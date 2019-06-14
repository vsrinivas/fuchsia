// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/hermetic-compute/hermetic-engine.h>

#include <numeric>

struct TestEngine : public HermeticComputeEngine<
    TestEngine, std::basic_string_view<uint8_t>> {
    int64_t operator()(const std::basic_string_view<uint8_t>& data) {
        return std::accumulate(data.begin(), data.end(), 0);
    }
};
