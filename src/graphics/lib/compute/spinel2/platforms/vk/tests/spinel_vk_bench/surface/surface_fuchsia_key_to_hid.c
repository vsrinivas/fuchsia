// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "surface_fuchsia_key_to_hid.h"

#include "common/macros.h"
#include "surface/surface_types.h"

//
// We know Fuchsia keys are defined as:
//
//   The ordinal index for enum elements is derived from the USB HID Usage
//   Tables at the time of definition. It is a 32 bit unsigned integer
//   representing the USB HID Usage where the low 16 bits are the USB HID Usage
//   ID and the high 16 bits are the USB HID Usage Page.
//
// For now, just return the low byte.
//

#define SURFACE_FUCHSIA_KEY_MASK 0xFF

uint32_t
surface_fuchsia_key_to_hid(uint32_t key)
{
  uint32_t const key_masked = (key & SURFACE_FUCHSIA_KEY_MASK);

  return key_masked;
}

//
//
//
