// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_PROXY_SERVICE_H_
#define NET_PROXY_PROXY_SERVICE_H_
#pragma once

#include <vector>

#include "base/gtest_prod_util.h"
#include "base/ref_counted.h"
#include "base/scoped_ptr.h"
#include "base/waitable_event.h"
#include "net/base/completion_callback.h"
#include "net/base/network_change_notifier.h"
#include "net/base/net_log.h"
#include "net/proxy/proxy_config_service.h"
#include "net/proxy/proxy_server.h"
#include "net/proxy/proxy_info.h"

class GURL;
class MessageLoop;
class URLRequestContext;

namespace net {

class InitProxyResolver;
class ProxyResolver;
class ProxyScriptFetcher;

// This class can be used to resolve the proxy server to use when loading a
// HTTP(S) URL.  It uses the given ProxyResolver to handle the actual proxy
// resolution.  See ProxyResolverV8 for example.
class ProxyService : public base::RefCountedThreadSafe<ProxyService>,
                     public NetworkChangeNotifier::Observer,
                     public ProxyConfigService::Observer {
 public:
  // The instance takes ownership of |config_service| and |resolver|.
  // |net_log| is a possibly NULL destination to send log events to. It must
  // remain alive for the lifetime of this ProxyService.
  ProxyService(ProxyConfigService* config_service,
               ProxyResolver* resolver,
               NetLog* net_log);

  // Used internally to handle PAC queries.
  // TODO(eroman): consider naming this simply "Request".
  class PacRequest;

  // Returns ERR_IO_PENDING if the proxy information could not be provided
  // synchronously, to indicate that the result will be available when the
  // callback is run.  The callback is run on the thread that calls
  // ResolveProxy.
  //
  // The caller is responsible for ensuring that |results| and |callback|
  // remain valid until the callback is run or until |pac_request| is cancelled
  // via CancelPacRequest.  |pac_request| is only valid while the completion
  // callback is still pending. NULL can be passed for |pac_request| if
  // the caller will not need to cancel the request.
  //
  // We use the three possible proxy access types in the following order,
  // doing fallback if one doesn't work.  See "init_proxy_resolver.h"
  // for the specifics.
  //   1.  WPAD auto-detection
  //   2.  PAC URL
  //   3.  named proxy
  //
  // Profiling information for the request is saved to |net_log| if non-NULL.
  int ResolveProxy(const GURL& url,
                   ProxyInfo* results,
                   CompletionCallback* callback,
                   PacRequest** pac_request,
                   const BoundNetLog& net_log);

  // This method is called after a failure to connect or resolve a host name.
  // It gives the proxy service an opportunity to reconsider the proxy to use.
  // The |results| parameter contains the results returned by an earlier call
  // to ResolveProxy.  The semantics of this call are otherwise similar to
  // ResolveProxy.
  //
  // NULL can be passed for |pac_request| if the caller will not need to
  // cancel the request.
  //
  // Returns ERR_FAILED if there is not another proxy config to try.
  //
  // Profiling information for the request is saved to |net_log| if non-NULL.
  int ReconsiderProxyAfterError(const GURL& url,
                                ProxyInfo* results,
                                CompletionCallback* callback,
                                PacRequest** pac_request,
                                const BoundNetLog& net_log);

  // Call this method with a non-null |pac_request| to cancel the PAC request.
  void CancelPacRequest(PacRequest* pac_request);

  // Sets the ProxyScriptFetcher dependency. This is needed if the ProxyResolver
  // is of type ProxyResolverWithoutFetch. ProxyService takes ownership of
  // |proxy_script_fetcher|.
  void SetProxyScriptFetcher(ProxyScriptFetcher* proxy_script_fetcher);
  ProxyScriptFetcher* GetProxyScriptFetcher() const;

  // Tells this ProxyService to start using a new ProxyConfigService to
  // retrieve its ProxyConfig from. The new ProxyConfigService will immediately
  // be queried for new config info which will be used for all subsequent
  // ResolveProxy calls. ProxyService takes ownership of
  // |new_proxy_config_service|.
  void ResetConfigService(ProxyConfigService* new_proxy_config_service);

  // Tells the resolver to purge any memory it does not need.
  void PurgeMemory();

  // Returns true if we have called UpdateConfig() at least once.
  bool config_has_been_initialized() const {
    return config_.id() != ProxyConfig::INVALID_ID;
  }

  // Returns the last configuration fetched from ProxyConfigService.
  const ProxyConfig& config() {
    return config_;
  }

  // Returns the map of proxies which have been marked as "bad".
  const ProxyRetryInfoMap& proxy_retry_info() const {
    return proxy_retry_info_;
  }

  // Clears the list of bad proxy servers that has been cached.
  void ClearBadProxiesCache() {
    proxy_retry_info_.clear();
  }

  // Forces refetching the proxy configuration, and applying it.
  // This re-does everything from fetching the system configuration,
  // to downloading and testing the PAC files.
  void ForceReloadProxyConfig();

  // Creates a proxy service that polls |proxy_config_service| to notice when
  // the proxy settings change. We take ownership of |proxy_config_service|.
  // Iff |use_v8_resolver| is true, then the V8 implementation is
  // used.
  //
  // |num_pac_threads| specifies the maximum number of threads to use for
  // executing PAC scripts. Threads are created lazily on demand.
  // If |0| is specified, then a default number of threads will be selected.
  //
  // Having more threads avoids stalling proxy resolve requests when the
  // PAC script takes a while to run. This is particularly a problem when PAC
  // scripts do synchronous DNS resolutions, since that can take on the order
  // of seconds.
  //
  // However, the disadvantages of using more than 1 thread are:
  //   (a) can cause compatibility issues for scripts that rely on side effects
  //       between runs (such scripts should not be common though).
  //   (b) increases the memory used by proxy resolving, as each thread will
  //       duplicate its own script context.

  // |url_request_context| is only used when use_v8_resolver is true:
  // it specifies the URL request context that will be used if a PAC
  // script needs to be fetched.
  // |io_loop| points to the IO thread's message loop. It is only used
  // when pc is NULL.
  // ##########################################################################
  // # See the warnings in net/proxy/proxy_resolver_v8.h describing the
  // # multi-threading model. In order for this to be safe to use, *ALL* the
  // # other V8's running in the process must use v8::Locker.
  // ##########################################################################
  static ProxyService* Create(
      ProxyConfigService* proxy_config_service,
      bool use_v8_resolver,
      size_t num_pac_threads,
      URLRequestContext* url_request_context,
      NetLog* net_log,
      MessageLoop* io_loop);

  // Convenience method that creates a proxy service using the
  // specified fixed settings. |pc| must not be NULL.
  static ProxyService* CreateFixed(const ProxyConfig& pc);

  // Creates a proxy service that always fails to fetch the proxy configuration,
  // so it falls back to direct connect.
  static ProxyService* CreateNull();

  // Creates a config service appropriate for this platform that fetches the
  // system proxy settings.
  static ProxyConfigService* CreateSystemProxyConfigService(
      MessageLoop* io_loop, MessageLoop* file_loop);

 private:
  friend class base::RefCountedThreadSafe<ProxyService>;
  FRIEND_TEST_ALL_PREFIXES(ProxyServiceTest, UpdateConfigAfterFailedAutodetect);
  FRIEND_TEST_ALL_PREFIXES(ProxyServiceTest, UpdateConfigFromPACToDirect);
  friend class PacRequest;

  // TODO(eroman): change this to a std::set. Note that this requires updating
  // some tests in proxy_service_unittest.cc such as:
  //   ProxyServiceTest.InitialPACScriptDownload
  // which expects requests to finish in the order they were added.
  typedef std::vector<scoped_refptr<PacRequest> > PendingRequests;

  enum State {
    STATE_NONE,
    STATE_WAITING_FOR_PROXY_CONFIG,
    STATE_WAITING_FOR_INIT_PROXY_RESOLVER,
    STATE_READY,
  };

  ~ProxyService();

  // Resets all the variables associated with the current proxy configuration,
  // and rewinds the current state to |STATE_NONE|. Returns the previous value
  // of |current_state_|.
  State ResetProxyConfig();

  // Retrieves the current proxy configuration from the ProxyConfigService, and
  // starts initializing for it.
  void ApplyProxyConfigIfAvailable();

  // Callback for when the proxy resolver has been initialized with a
  // PAC script.
  void OnInitProxyResolverComplete(int result);

  // Returns ERR_IO_PENDING if the request cannot be completed synchronously.
  // Otherwise it fills |result| with the proxy information for |url|.
  // Completing synchronously means we don't need to query ProxyResolver.
  int TryToCompleteSynchronously(const GURL& url, ProxyInfo* result);

  // Cancels all of the requests sent to the ProxyResolver. These will be
  // restarted when calling ResumeAllPendingRequests().
  void SuspendAllPendingRequests();

  // Advances the current state to |STATE_READY|, and resumes any pending
  // requests which had been stalled waiting for initialization to complete.
  void SetReady();

  // Returns true if |pending_requests_| contains |req|.
  bool ContainsPendingRequest(PacRequest* req);

  // Removes |req| from the list of pending requests.
  void RemovePendingRequest(PacRequest* req);

  // Called when proxy resolution has completed (either synchronously or
  // asynchronously). Handles logging the result, and cleaning out
  // bad entries from the results list.
  int DidFinishResolvingProxy(ProxyInfo* result,
                              int result_code,
                              const BoundNetLog& net_log);

  // NetworkChangeNotifier::Observer
  // When this is called, we re-fetch PAC scripts and re-run WPAD.
  virtual void OnIPAddressChanged();

  // ProxyConfigService::Observer
  virtual void OnProxyConfigChanged(const ProxyConfig& config);

  scoped_ptr<ProxyConfigService> config_service_;
  scoped_ptr<ProxyResolver> resolver_;

  // We store the proxy config and a counter (ID) that is incremented each time
  // the config changes.
  ProxyConfig config_;

  // Increasing ID to give to the next ProxyConfig that we set.
  int next_config_id_;

  // Indicates whether the ProxyResolver should be sent requests.
  bool should_use_proxy_resolver_;

  // The time when the proxy configuration was last read from the system.
  base::TimeTicks config_last_update_time_;

  // Map of the known bad proxies and the information about the retry time.
  ProxyRetryInfoMap proxy_retry_info_;

  // Set of pending/inprogress requests.
  PendingRequests pending_requests_;

  // The fetcher to use when downloading PAC scripts for the ProxyResolver.
  // This dependency can be NULL if our ProxyResolver has no need for
  // external PAC script fetching.
  scoped_ptr<ProxyScriptFetcher> proxy_script_fetcher_;

  // Callback for when |init_proxy_resolver_| is done.
  CompletionCallbackImpl<ProxyService> init_proxy_resolver_callback_;

  // Helper to download the PAC script (wpad + custom) and apply fallback rules.
  //
  // Note that the declaration is important here: |proxy_script_fetcher_| and
  // |proxy_resolver_| must outlive |init_proxy_resolver_|.
  scoped_ptr<InitProxyResolver> init_proxy_resolver_;

  State current_state_;

  // This is the log where any events generated by |init_proxy_resolver_| are
  // sent to.
  NetLog* net_log_;

  DISALLOW_COPY_AND_ASSIGN(ProxyService);
};

// Wrapper for invoking methods on a ProxyService synchronously.
class SyncProxyServiceHelper
    : public base::RefCountedThreadSafe<SyncProxyServiceHelper> {
 public:
  SyncProxyServiceHelper(MessageLoop* io_message_loop,
                         ProxyService* proxy_service);

  int ResolveProxy(const GURL& url,
                   ProxyInfo* proxy_info,
                   const BoundNetLog& net_log);
  int ReconsiderProxyAfterError(const GURL& url,
                                ProxyInfo* proxy_info,
                                const BoundNetLog& net_log);

 private:
  friend class base::RefCountedThreadSafe<SyncProxyServiceHelper>;

  ~SyncProxyServiceHelper() {}

  void StartAsyncResolve(const GURL& url, const BoundNetLog& net_log);
  void StartAsyncReconsider(const GURL& url, const BoundNetLog& net_log);

  void OnCompletion(int result);

  MessageLoop* io_message_loop_;
  ProxyService* proxy_service_;

  base::WaitableEvent event_;
  CompletionCallbackImpl<SyncProxyServiceHelper> callback_;
  ProxyInfo proxy_info_;
  int result_;
};

}  // namespace net

#endif  // NET_PROXY_PROXY_SERVICE_H_
