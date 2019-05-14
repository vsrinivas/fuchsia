// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>

// A no-op fuzz target function.  This is provided to prove fuzzers can be built.
// TODO(aarongreen): Also run the fuzzer with an empty input.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    return 0;
}
