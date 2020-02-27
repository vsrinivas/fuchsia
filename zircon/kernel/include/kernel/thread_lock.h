// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// Declares the lockdep instrumented global thread lock.

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_THREAD_LOCK_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_THREAD_LOCK_H_

#include <kernel/lockdep.h>

extern spin_lock_t thread_lock;
DECLARE_SINGLETON_LOCK_WRAPPER(ThreadLock, thread_lock,
                               (LockFlagsReportingDisabled | LockFlagsTrackingDisabled));

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_THREAD_LOCK_H_
