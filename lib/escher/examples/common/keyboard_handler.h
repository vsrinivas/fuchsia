// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

class KeyboardHandler {
 public:
  KeyboardHandler() {}

  // Register a callback to fire when |key| is pressed.  Key must contain either
  // a single alpha-numeric character (uppercase only), or one of the special
  // values "ESCAPE", "SPACE", and "RETURN".
  void SetCallback(std::string key, std::function<void()> func) {
    callbacks_[key] = std::move(func);
  }

  void MaybeFireCallback(std::string key) {
    auto it = callbacks_.find(key);
    if (it != callbacks_.end()) {
      it->second();
    }
  }

 private:
  std::unordered_map<std::string, std::function<void()>> callbacks_;
};
