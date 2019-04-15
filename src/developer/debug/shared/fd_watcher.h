// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace debug_ipc {

// Simple observer class for readable/writable state changes on some
// communication or storage channel.
class FDWatcher {
 public:
  virtual void OnFDReady(int fd, bool read, bool write, bool err) {}
};

}  // namespace debug_ipc
