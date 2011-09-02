// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/engine/net/server_connection_manager.h"

#include <errno.h>

#include <ostream>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "build/build_config.h"
#include "chrome/browser/sync/engine/net/url_translator.h"
#include "chrome/browser/sync/engine/syncer.h"
#include "chrome/browser/sync/engine/syncproto.h"
#include "chrome/browser/sync/protocol/sync.pb.h"
#include "chrome/browser/sync/syncable/directory_manager.h"
#include "chrome/common/net/http_return.h"
#include "googleurl/src/gurl.h"

namespace browser_sync {

using std::ostream;
using std::string;
using std::vector;

static const char kSyncServerSyncPath[] = "/command/";

// At the /time/ path of the sync server, we expect to find a very simple
// time of day service that we can use to synchronize the local clock with
// server time.
static const char kSyncServerGetTimePath[] = "/time";

HttpResponse::HttpResponse()
    : response_code(kUnsetResponseCode),
      content_length(kUnsetContentLength),
      payload_length(kUnsetPayloadLength),
      server_status(NONE) {}

#define ENUM_CASE(x) case x: return #x; break

const char* HttpResponse::GetServerConnectionCodeString(
    ServerConnectionCode code) {
  switch (code) {
    ENUM_CASE(NONE);
    ENUM_CASE(CONNECTION_UNAVAILABLE);
    ENUM_CASE(IO_ERROR);
    ENUM_CASE(SYNC_SERVER_ERROR);
    ENUM_CASE(SYNC_AUTH_ERROR);
    ENUM_CASE(SERVER_CONNECTION_OK);
    ENUM_CASE(RETRY);
  }
  NOTREACHED();
  return "";
}

#undef ENUM_CASE

bool ServerConnectionManager::Post::ReadBufferResponse(
    string* buffer_out,
    HttpResponse* response,
    bool require_response) {
  if (RC_REQUEST_OK != response->response_code) {
    response->server_status = HttpResponse::SYNC_SERVER_ERROR;
    return false;
  }

  if (require_response && (1 > response->content_length))
    return false;

  const int64 bytes_read = ReadResponse(buffer_out,
      static_cast<int>(response->content_length));
  if (bytes_read != response->content_length) {
    response->server_status = HttpResponse::IO_ERROR;
    return false;
  }
  return true;
}

bool ServerConnectionManager::Post::ReadDownloadResponse(
    HttpResponse* response,
    string* buffer_out) {
  const int64 bytes_read = ReadResponse(buffer_out,
      static_cast<int>(response->content_length));

  if (bytes_read != response->content_length) {
    LOG(ERROR) << "Mismatched content lengths, server claimed " <<
        response->content_length << ", but sent " << bytes_read;
    response->server_status = HttpResponse::IO_ERROR;
    return false;
  }
  return true;
}

namespace {

string StripTrailingSlash(const string& s) {
  int stripped_end_pos = s.size();
  if (s.at(stripped_end_pos - 1) == '/') {
    stripped_end_pos = stripped_end_pos - 1;
  }

  return s.substr(0, stripped_end_pos);
}

}  // namespace

// TODO(chron): Use a GURL instead of string concatenation.
string ServerConnectionManager::Post::MakeConnectionURL(
    const string& sync_server,
    const string& path,
    bool use_ssl) const {
  string connection_url = (use_ssl ? "https://" : "http://");
  connection_url += sync_server;
  connection_url = StripTrailingSlash(connection_url);
  connection_url += path;

  return connection_url;
}

int ServerConnectionManager::Post::ReadResponse(string* out_buffer,
                                                int length) {
  int bytes_read = buffer_.length();
  CHECK(length <= bytes_read);
  out_buffer->assign(buffer_);
  return bytes_read;
}

ScopedServerStatusWatcher::ScopedServerStatusWatcher(
    ServerConnectionManager* conn_mgr, HttpResponse* response)
    : conn_mgr_(conn_mgr),
      response_(response),
      server_reachable_(conn_mgr->server_reachable_) {
  response->server_status = conn_mgr->server_status_;
}

ScopedServerStatusWatcher::~ScopedServerStatusWatcher() {
  if (conn_mgr_->server_status_ != response_->server_status) {
    conn_mgr_->server_status_ = response_->server_status;
    conn_mgr_->NotifyStatusChanged();
    return;
  }
  // Notify if we've gone on or offline.
  if (server_reachable_ != conn_mgr_->server_reachable_)
    conn_mgr_->NotifyStatusChanged();
}

ServerConnectionManager::ServerConnectionManager(
    const string& server,
    int port,
    bool use_ssl,
    const string& user_agent)
    : sync_server_(server),
      sync_server_port_(port),
      user_agent_(user_agent),
      use_ssl_(use_ssl),
      proto_sync_path_(kSyncServerSyncPath),
      get_time_path_(kSyncServerGetTimePath),
      error_count_(0),
      server_status_(HttpResponse::NONE),
      server_reachable_(false) {
}

ServerConnectionManager::~ServerConnectionManager() {
}

void ServerConnectionManager::NotifyStatusChanged() {
  DCHECK(CalledOnValidThread());
  FOR_EACH_OBSERVER(ServerConnectionEventListener, listeners_,
     OnServerConnectionEvent(
         ServerConnectionEvent(server_status_, server_reachable_)));
}

bool ServerConnectionManager::PostBufferWithCachedAuth(
    PostBufferParams* params, ScopedServerStatusWatcher* watcher) {
  DCHECK(CalledOnValidThread());
  string path =
      MakeSyncServerPath(proto_sync_path(), MakeSyncQueryString(client_id_));
  return PostBufferToPath(params, path, auth_token(), watcher);
}

bool ServerConnectionManager::PostBufferToPath(PostBufferParams* params,
    const string& path, const string& auth_token,
    ScopedServerStatusWatcher* watcher) {
  DCHECK(CalledOnValidThread());
  DCHECK(watcher != NULL);

  if (auth_token.empty()) {
    params->response.server_status = HttpResponse::SYNC_AUTH_ERROR;
    return false;
  }

  scoped_ptr<Post> post(MakePost());
  bool ok = post->Init(path.c_str(), auth_token, params->buffer_in,
                       &params->response);

  if (params->response.server_status == HttpResponse::SYNC_AUTH_ERROR) {
    InvalidateAndClearAuthToken();
  }

  if (!ok || RC_REQUEST_OK != params->response.response_code) {
    IncrementErrorCount();
    return false;
  }

  if (post->ReadBufferResponse(&params->buffer_out, &params->response, true)) {
    params->response.server_status = HttpResponse::SERVER_CONNECTION_OK;
    server_reachable_ = true;
    return true;
  }
  return false;
}

bool ServerConnectionManager::CheckTime(int32* out_time) {
  DCHECK(CalledOnValidThread());
  // Verify that the server really is reachable by checking the time. We need
  // to do this because of wifi interstitials that intercept messages from the
  // client and return HTTP OK instead of a redirect.
  HttpResponse response;
  ScopedServerStatusWatcher watcher(this, &response);
  string post_body = "command=get_time";

  for (int i = 0 ; i < 3;  i++) {
    scoped_ptr<Post> post(MakePost());

    // Note that the server's get_time path doesn't require authentication.
    string get_time_path =
        MakeSyncServerPath(kSyncServerGetTimePath, post_body);
    VLOG(1) << "Requesting get_time from:" << get_time_path;

    string blank_post_body;
    bool ok = post->Init(get_time_path.c_str(), blank_post_body,
        blank_post_body, &response);
    if (!ok) {
      VLOG(1) << "Unable to check the time";
      continue;
    }
    string time_response;
    time_response.resize(
        static_cast<string::size_type>(response.content_length));
    ok = post->ReadDownloadResponse(&response, &time_response);
    if (!ok || string::npos !=
        time_response.find_first_not_of("0123456789")) {
      LOG(ERROR) << "unable to read a non-numeric response from get_time:"
            << time_response;
      continue;
    }
    *out_time = atoi(time_response.c_str());
    VLOG(1) << "Server was reachable.";
    return true;
  }
  IncrementErrorCount();
  return false;
}

bool ServerConnectionManager::IsServerReachable() {
  DCHECK(CalledOnValidThread());
  int32 time;
  return CheckTime(&time);
}

bool ServerConnectionManager::IsUserAuthenticated() {
  DCHECK(CalledOnValidThread());
  return IsGoodReplyFromServer(server_status_);
}

bool ServerConnectionManager::CheckServerReachable() {
  DCHECK(CalledOnValidThread());
  const bool server_is_reachable = IsServerReachable();
  if (server_reachable_ != server_is_reachable) {
    server_reachable_ = server_is_reachable;
    NotifyStatusChanged();
  }
  return server_is_reachable;
}

bool ServerConnectionManager::IncrementErrorCount() {
  DCHECK(CalledOnValidThread());
  error_count_++;

  if (error_count_ > kMaxConnectionErrorsBeforeReset) {
    error_count_ = 0;

    if (!IsServerReachable()) {
      LOG(WARNING) << "Too many connection failures, server is not reachable. "
                   << "Resetting connections.";
    } else {
      LOG(WARNING) << "Multiple connection failures while server is reachable.";
    }
    return false;
  }

  return true;
}

void ServerConnectionManager::SetServerParameters(const string& server_url,
                                                  int port,
                                                  bool use_ssl) {
  DCHECK(CalledOnValidThread());
  sync_server_ = server_url;
  sync_server_port_ = port;
  use_ssl_ = use_ssl;
}

// Returns the current server parameters in server_url and port.
void ServerConnectionManager::GetServerParameters(string* server_url,
                                                  int* port,
                                                  bool* use_ssl) const {
  if (server_url != NULL)
    *server_url = sync_server_;
  if (port != NULL)
    *port = sync_server_port_;
  if (use_ssl != NULL)
    *use_ssl = use_ssl_;
}

std::string ServerConnectionManager::GetServerHost() const {
  string server_url;
  int port;
  bool use_ssl;
  GetServerParameters(&server_url, &port, &use_ssl);
  // For unit tests.
  if (server_url.empty())
    return std::string();
  // We just want the hostname, so we don't need to switch on use_ssl.
  server_url = "http://" + server_url;
  GURL gurl(server_url);
  DCHECK(gurl.is_valid()) << gurl;
  return gurl.host();
}

void ServerConnectionManager::AddListener(
    ServerConnectionEventListener* listener) {
  DCHECK(CalledOnValidThread());
  listeners_.AddObserver(listener);
}

void ServerConnectionManager::RemoveListener(
    ServerConnectionEventListener* listener) {
  DCHECK(CalledOnValidThread());
  listeners_.RemoveObserver(listener);
}

ServerConnectionManager::Post* ServerConnectionManager::MakePost() {
  return NULL;  // For testing.
}

bool FillMessageWithShareDetails(sync_pb::ClientToServerMessage* csm,
                                 syncable::DirectoryManager* manager,
                                 const std::string& share) {
  syncable::ScopedDirLookup dir(manager, share);
  if (!dir.good()) {
    VLOG(1) << "Dir lookup failed";
    return false;
  }
  string birthday = dir->store_birthday();
  if (!birthday.empty())
    csm->set_store_birthday(birthday);
  csm->set_share(share);
  return true;
}

std::ostream& operator << (std::ostream& s, const struct HttpResponse& hr) {
  s << " Response Code (bogus on error): " << hr.response_code;
  s << " Content-Length (bogus on error): " << hr.content_length;
  s << " Server Status: " << hr.server_status;
  return s;
}

}  // namespace browser_sync
