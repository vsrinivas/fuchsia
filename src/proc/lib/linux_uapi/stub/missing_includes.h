// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PROC_LIB_LINUX_UAPI_STUB_MISSING_INCLUDES_H_
#define SRC_PROC_LIB_LINUX_UAPI_STUB_MISSING_INCLUDES_H_

// Adding includes that are not detected by rust-bindings because they are
// defined using functions

#include <asm/ioctls.h>

const __u32 _TIOCSPTLCK = TIOCSPTLCK;
#undef TIOCSPTLCK
const __u32 TIOCSPTLCK = _TIOCSPTLCK;

const __u32 _TIOCGPTLCK = TIOCGPTLCK;
#undef TIOCGPTLCK
const __u32 TIOCGPTLCK = _TIOCGPTLCK;

const __u32 _TIOCGPKT = TIOCGPKT;
#undef TIOCGPKT
const __u32 TIOCGPKT = _TIOCGPKT;

const __u32 _TIOCSIG = TIOCSIG;
#undef TIOCSIG
const __u32 TIOCSIG = _TIOCSIG;

const __u32 _TIOCGPTN = TIOCGPTN;
#undef TIOCGPTN
const __u32 TIOCGPTN = _TIOCGPTN;

#endif  // SRC_PROC_LIB_LINUX_UAPI_STUB_MISSING_INCLUDES_H_
