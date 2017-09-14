// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>

#define TA_CAP(x) __TA_CAPABILITY(x)
#define TA_GUARDED(x) __TA_GUARDED(x)
#define TA_ACQ(...) __TA_ACQUIRE(__VA_ARGS__)
#define TA_ACQ_BEFORE(...) __TA_ACQUIRED_BEFORE(__VA_ARGS__)
#define TA_ACQ_AFTER(...) __TA_ACQUIRED_AFTER(__VA_ARGS__)
#define TA_REL(...) __TA_RELEASE(__VA_ARGS__)
#define TA_REQ(...) __TA_REQUIRES(__VA_ARGS__)
#define TA_EXCL(...) __TA_EXCLUDES(__VA_ARGS__)
#define TA_RET_CAP(x) __TA_RETURN_CAPABILITY(x)
#define TA_SCOPED_CAP __TA_SCOPED_CAPABILITY
#define TA_NO_THREAD_SAFETY_ANALYSIS __TA_NO_THREAD_SAFETY_ANALYSIS
