// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

extern "C" void pw_assert_HandleFailure() { ZX_PANIC("PW_ASSERT failure"); }
