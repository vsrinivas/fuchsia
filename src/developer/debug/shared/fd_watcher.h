// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_FD_WATCHER_H_
#define SRC_DEVELOPER_DEBUG_SHARED_FD_WATCHER_H_

namespace debug_ipc {

// Simple observer class for readable/writable state changes on some communication or storage
// channel.
class FDWatcher {
 public:
  virtual void OnFDReady(int fd, bool read, bool write, bool err) {}
};

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_SHARED_FD_WATCHER_H_
