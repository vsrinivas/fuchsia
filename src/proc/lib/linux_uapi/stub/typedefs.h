// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PROC_LIB_LINUX_UAPI_STUB_TYPEDEFS_H_
#define SRC_PROC_LIB_LINUX_UAPI_STUB_TYPEDEFS_H_

// This file contains typedefs required by linux headers that may break convention and rely
// on libc types, which we do not include in bindgen.

#include <linux/types.h>

// Binder uses the libc types pid_t and uid_t.
typedef __kernel_pid_t pid_t;
typedef __kernel_uid_t uid_t;

// Binder uses this to declare packed structs.
#define __packed __attribute__((__packed__))

#endif  // SRC_PROC_LIB_LINUX_UAPI_STUB_TYPEDEFS_H_
