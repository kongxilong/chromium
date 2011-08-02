// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_INPUT_IME_API_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_INPUT_IME_API_H_
#pragma once

#include "chrome/browser/extensions/extension_function.h"

#include "base/memory/singleton.h"
#include "base/values.h"
#include "chrome/browser/chromeos/input_method/input_method_engine.h"
#include "chrome/common/extensions/extension.h"

#include <map>
#include <string>
#include <vector>

class Profile;

namespace chromeos {
class InputMethodEngine;
class ImeObserver;
}

class ExtensionInputImeEventRouter {
 public:
  static ExtensionInputImeEventRouter* GetInstance();
  void Init();

  bool RegisterIme(Profile* profile,
                   const std::string& extension_id,
                   const Extension::InputComponentInfo& component);
  chromeos::InputMethodEngine* GetEngine(const std::string& extension_id,
                                         const std::string& engine_id);
  chromeos::InputMethodEngine* GetActiveEngine(const std::string& extension_id);


 private:
  friend struct DefaultSingletonTraits<ExtensionInputImeEventRouter>;

  ExtensionInputImeEventRouter();
  ~ExtensionInputImeEventRouter();

  std::map<std::string, std::map<std::string, chromeos::InputMethodEngine*> >
      engines_;
  std::map<std::string, std::map<std::string, chromeos::ImeObserver*> >
      observers_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionInputImeEventRouter);
};

class SetCompositionFunction : public AsyncExtensionFunction {
 public:
  virtual bool RunImpl();
  DECLARE_EXTENSION_FUNCTION_NAME(
      "experimental.input.setComposition");
};

class ClearCompositionFunction : public AsyncExtensionFunction {
 public:
  virtual bool RunImpl();
  DECLARE_EXTENSION_FUNCTION_NAME(
      "experimental.input.clearComposition");
};

class CommitTextFunction : public AsyncExtensionFunction {
 public:
  virtual bool RunImpl();
  DECLARE_EXTENSION_FUNCTION_NAME(
      "experimental.input.commitText");
};

class SetCandidateWindowPropertiesFunction : public AsyncExtensionFunction {
 public:
  virtual bool RunImpl();
  DECLARE_EXTENSION_FUNCTION_NAME(
      "experimental.input.setCandidateWindowProperties");
};

class SetCandidatesFunction : public AsyncExtensionFunction {
 public:
  virtual bool RunImpl();
  DECLARE_EXTENSION_FUNCTION_NAME(
      "experimental.input.setCandidates");
 private:
  bool ReadCandidates(
      ListValue* candidates,
      std::vector<chromeos::InputMethodEngine::Candidate>* output);
};

class SetCursorPositionFunction : public AsyncExtensionFunction {
 public:
  virtual bool RunImpl();
  DECLARE_EXTENSION_FUNCTION_NAME(
      "experimental.input.setCursorPosition");
};

class SetMenuItemsFunction : public AsyncExtensionFunction {
 public:
  virtual bool RunImpl();
  DECLARE_EXTENSION_FUNCTION_NAME(
      "experimental.input.setMenuItems");
};

class UpdateMenuItemsFunction : public AsyncExtensionFunction {
 public:
  virtual bool RunImpl();
  DECLARE_EXTENSION_FUNCTION_NAME(
      "experimental.input.updateMenuItems");
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_INPUT_IME_API_H_
