// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>

#ifdef __Fuchsia__
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#else
#include <mutex>
#endif

namespace minfs {

#ifdef __Fuchsia__
using Mutex = fbl::Mutex;
using AutoLock = fbl::AutoLock<fbl::Mutex>;
#else
using Mutex = std::mutex;

class AutoLock {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(AutoLock);

  AutoLock(std::mutex* mutex) : mutex_(mutex) { mutex_->lock(); }
  ~AutoLock() { mutex_->unlock(); }

 private:
  std::mutex* mutex_;
};

#endif

}  // namespace minfs
