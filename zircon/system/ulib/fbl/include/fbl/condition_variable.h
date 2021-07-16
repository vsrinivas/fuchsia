// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_CONDITION_VARIABLE_H_
#define FBL_CONDITION_VARIABLE_H_

#ifdef __cplusplus

// ConditionVariable is a C++ helper class intended to wrap a condition variable synchronization
// primitive. It is also responsible for automatically initializing and destroying the internal
// object.
//
// This object is currently only supported in userspace.
#ifndef _KERNEL

#include <threads.h>

#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <zircon/compiler.h>

namespace fbl {

class ConditionVariable {
 public:
  ConditionVariable() { cnd_init(&cond_); }
  ~ConditionVariable() { cnd_destroy(&cond_); }
  DISALLOW_COPY_ASSIGN_AND_MOVE(ConditionVariable);

  void Wait(Mutex* mutex) __TA_REQUIRES(mutex) { cnd_wait(&cond_, mutex->GetInternal()); }

  void Signal() { cnd_signal(&cond_); }

  void Broadcast() { cnd_broadcast(&cond_); }

  cnd_t* get() { return &cond_; }

 private:
  cnd_t cond_;
};

}  // namespace fbl

#endif  // ifndef _KERNEL
#endif  // ifdef __cplusplus

#endif  // FBL_CONDITION_VARIABLE_H_
