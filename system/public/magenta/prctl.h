// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__x86_64__)

#define ARCH_SET_FS 0
#define ARCH_GET_FS 1
#define ARCH_SET_GS 2
#define ARCH_GET_GS 3
#define ARCH_GET_TSC_TICKS_PER_MS 4

#elif defined(__aarch64__)

#define ARCH_SET_TPIDRRO_EL0 0

#elif defined(__arm__)

#define ARCH_SET_CP15_READONLY 0

#else
#error "need to define PRCTL enum for your architecture"
#endif

#ifdef __cplusplus
}
#endif
