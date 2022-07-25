// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <asm.h>

#include "sanitizer-stubs.h"
#include "sancov-stubs.h"

// See the end of dynlink.c for what this is about.

#define SANCOV_STUB(name) sanitizer_stub __sanitizer_cov_##name;
SANCOV_STUBS
