// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/hermetic-compute/hermetic-engine.h>
#include <string_view>

using byte_view = std::basic_string_view<std::byte>;

struct DecompressorEngine
    : public HermeticComputeEngine<DecompressorEngine, byte_view, std::byte*, size_t> {
  int64_t operator()(byte_view input, std::byte* output, size_t output_size);
};
