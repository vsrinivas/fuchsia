// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#if defined(__x86_64__)
#include "garnet/bin/debug_agent/arch_x64.h"
#elif defined(__aarch64__)
#include "garnet/bin/debug_agent/arch_arm64.h"
#else
#error
#endif

