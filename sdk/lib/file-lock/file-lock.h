// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FILE_LOCK_FILE_LOCK_H_
#define LIB_FILE_LOCK_FILE_LOCK_H_

#include <lib/fit/function.h>
#include <zircon/types.h>

#include <map>
#include <mutex>
#include <set>

namespace file_lock {

using lock_completer_t = fit::callback<void(zx_status_t status)>;

enum LockType {
  READ,
  WRITE,
  UNLOCK,
};

class LockRequest final {
 public:
  LockRequest(LockType type, bool wait) : type_(type), wait_(wait) {}
  bool wait() const { return wait_; }
  LockType type() const { return type_; }

 private:
  LockType type_;
  bool wait_;
};

class FileLock final {
 public:
  FileLock() : exclusive_(ZX_KOID_INVALID), running_(true) {}
  ~FileLock();

  void Lock(zx_koid_t owner, LockRequest& req, lock_completer_t& completer);
  bool Forget(zx_koid_t owner);
  bool NoLocksHeld();

 private:
  std::mutex lock_mtx_;

  std::map<zx_koid_t, lock_completer_t> pending_shared_;
  std::map<zx_koid_t, lock_completer_t> pending_exclusive_;

  // shared lock <= shared.size() > 0
  // exclusive lock <= exclusive_ != ZX_KOID_INVALID
  std::set<zx_koid_t> shared_;
  zx_koid_t exclusive_;
  bool running_;

  bool LockInProgress(zx_koid_t owner) {
    return pending_exclusive_.find(owner) != pending_exclusive_.end() ||
           pending_shared_.find(owner) != pending_shared_.end();
  }
};

}  // namespace file_lock

#endif  // LIB_FILE_LOCK_FILE_LOCK_H_
