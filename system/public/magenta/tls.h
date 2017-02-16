// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// These constants are part of the C/C++ ABI known to compilers for
// *-fuchsia targets.  These are offsets from the thread pointer.

#if defined(__x86_64__)

#define MX_TLS_STACK_GUARD_OFFSET       0x10
#define MX_TLS_UNSAFE_SP_OFFSET         0x18

#elif defined(__aarch64__)

#define MX_TLS_STACK_GUARD_OFFSET       -0x10
#define MX_TLS_UNSAFE_SP_OFFSET         -0x8

#else

#error what architecture?

#endif
