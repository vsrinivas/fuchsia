// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

//
// Declares the lockdep instrumented global thread lock.
//
// Until more general C to C++ conversion is completed this header must only
// be included in C++ source files. In particular this header must not be
// included in kernel/wait.h or kernel/thread.h until the conversion is done.

#pragma once

#include <kernel/lockdep.h>
#include <kernel/thread.h>

DECLARE_SINGLETON_LOCK_WRAPPER(ThreadLock, thread_lock,
                               (LockFlagsReportingDisabled |
                                LockFlagsTrackingDisabled));
