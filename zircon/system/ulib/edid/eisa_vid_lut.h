// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define EISA_ID(a, b, c) \
  ((((uint32_t)(a)&0xFF) << 16) | (((uint32_t)(b)&0xFF) << 8) | ((uint32_t)(c)&0xFF))

// Lookup an EISA vendor name based on its assigned 3 character EISA vendor ID.
// Use the EISA_ID macro to generate the ID.  Returns NULL if no match is found
// in the LUT.
const char* lookup_eisa_vid(uint32_t eisa_vid);

__END_CDECLS
