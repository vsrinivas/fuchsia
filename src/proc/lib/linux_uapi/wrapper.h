// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PROC_LIB_LINUX_UAPI_WRAPPER_H_
#define SRC_PROC_LIB_LINUX_UAPI_WRAPPER_H_

// Skip this header for now. There are some tricky unions in here that we'll
// need to deal with later.
#define _UAPI_ASM_GENERIC_SIGINFO_H

#include <stddef.h>

#include <asm-generic/errno.h>
#include <asm-generic/ioctls.h>
#include <asm-generic/mman-common.h>
#include <asm-generic/termbits.h>
#include <asm-x86/asm/prctl.h>
#include <asm-x86/asm/unistd_64.h>
#include <linux/auxvec.h>
#include <linux/capability.h>
#include <linux/eventpoll.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/futex.h>
#include <linux/limits.h>
#include <linux/magic.h>
#include <linux/memfd.h>
#include <linux/mman.h>
#include <linux/poll.h>
#include <linux/prctl.h>
#include <linux/resource.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/socket.h>
#include <linux/stat.h>
#include <linux/time.h>
#include <linux/timerfd.h>
#include <linux/uio.h>
#include <linux/wait.h>
#include <linux/xattr.h>

#endif  // SRC_PROC_LIB_LINUX_UAPI_WRAPPER_H_
