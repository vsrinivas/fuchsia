// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "src/lib/rust_url/rust_url.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::string input(reinterpret_cast<const char*>(data), size);

  RustUrl url;
  url.Parse(input);
  url.Domain();  // should only be called if parsing succeeds but this asserts no UB if failed

  return 0;
}
