// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/librtc.h"

uint8_t to_bcd(uint8_t binary) { return (uint8_t)(((binary / 10) << 4) | (binary % 10)); }

uint8_t from_bcd(uint8_t bcd) { return ((bcd >> 4) * 10) + (bcd & 0xf); }
