// Copyright 2017 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD - style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace boring_crypto {

size_t FromHex(const char *hex, uint8_t* out, size_t max);

bool ChaChaUnitTests(void*);

} // namespace boring_crypto
