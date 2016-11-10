// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#ifdef __clang__
#define PRIx64 "llx"
#else
#define PRIx64 "lx"
#endif
