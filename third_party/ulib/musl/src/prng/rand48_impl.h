// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "libc.h"

extern unsigned short __seed48[7] ATTR_LIBC_VISIBILITY;
uint64_t __rand48_step(unsigned short* xi, unsigned short* lc) ATTR_LIBC_VISIBILITY;
