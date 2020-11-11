// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

// A simple fuzzer that will run forever.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) { return 0; }
