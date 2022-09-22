// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PROC_LIB_LINUX_UAPI_WRAPPER_H_
#define SRC_PROC_LIB_LINUX_UAPI_WRAPPER_H_

#include <stddef.h>
#include <typedefs.h>

#include <asm/ioctls.h>
#include <asm/poll.h>
#include <asm/prctl.h>
#include <asm/sigcontext.h>
#include <asm/signal.h>
#include <asm/socket.h>
#include <asm/ucontext.h>
#include <linux/android/binder.h>
#include <linux/auxvec.h>
#include <linux/bpf.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/eventpoll.h>
#include <linux/fb.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/futex.h>
#include <linux/inotify.h>
#include <linux/limits.h>
#include <linux/magic.h>
#include <linux/membarrier.h>
#include <linux/memfd.h>
#include <linux/mman.h>
#include <linux/poll.h>
#include <linux/prctl.h>
#include <linux/reboot.h>
#include <linux/resource.h>
#include <linux/sched.h>
#include <linux/sched/types.h>
#include <linux/securebits.h>
#include <linux/signal.h>
#include <linux/signalfd.h>
#include <linux/socket.h>
#include <linux/stat.h>
#include <linux/termios.h>
#include <linux/time.h>
#include <linux/timerfd.h>
#include <linux/uio.h>
#include <linux/unistd.h>
#include <linux/wait.h>
#include <linux/xattr.h>

#include "stub/missing_includes.h"

#endif  // SRC_PROC_LIB_LINUX_UAPI_WRAPPER_H_
