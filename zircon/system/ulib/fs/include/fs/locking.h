// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>

// Much filesystem code is built both for Fuchsia and for Linux or OS
// X hosts. This includes code instrumented with Clang's locking
// static analysis. The Linux and OS X host mutexes are not
// necessarily annotated properly for this static analysis. As such,
// fs provides a macro that wraps the locking annotations, or noops
// when they are not present. This wrapping is exposed in the public
// fs interface as the locking requirements are part of public
// interfaces.

#ifdef __Fuchsia__

#define FS_TA_EXCLUDES(...) __TA_EXCLUDES(__VA_ARGS__)
#define FS_TA_GUARDED(...) __TA_GUARDED(__VA_ARGS__)
#define FS_TA_REQUIRES(...) __TA_REQUIRES(__VA_ARGS__)
#define FS_TA_NO_THREAD_SAFETY_ANALYSIS __TA_NO_THREAD_SAFETY_ANALYSIS

#else

#define FS_TA_EXCLUDES(...)
#define FS_TA_GUARDED(...)
#define FS_TA_REQUIRES(...)
#define FS_TA_NO_THREAD_SAFETY_ANALYSIS

#endif
