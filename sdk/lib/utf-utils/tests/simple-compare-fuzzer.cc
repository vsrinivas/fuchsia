// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/utf-utils/internal/scalar.h>
#include <lib/utf-utils/utf-utils.h>

#include <cassert>

#include <fuzzer/FuzzedDataProvider.h>

// A simple fuzzer that compares the output of the (usually) SIMD-accelerated
// `utfutils_is_valid_utf8()` with the output of the internal scalar implementation
// `IsValidUtf8Scalar()`.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const char* data_char = reinterpret_cast<const char*>(data);

  bool actual = utfutils_is_valid_utf8(data_char, size);
  bool expected = utfutils::internal::IsValidUtf8Scalar(data_char, size);
  if (actual != expected) {
    __builtin_trap();
  }

  return 0;
}
