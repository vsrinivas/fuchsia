// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/assert.h>

extern "C" void __cxa_pure_virtual(void)
{
    MX_PANIC("pure virtual called\n");
}

