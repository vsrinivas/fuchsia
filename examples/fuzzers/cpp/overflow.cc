// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <memory>

// A simple fuzzer that detects a heap buffer overflow.

// The code under test. Normally this would be in a separate library.
namespace {

class Buffer final {
 public:
  Buffer(size_t size) : data_(new uint8_t[size]) {}
  Buffer() {}

  // Oops. No length check!
  void Write(const uint8_t *data, size_t size) { memcpy(data_.get(), data, size); }

 private:
  std::unique_ptr<uint8_t[]> data_;
};

}  // namespace

// The fuzz target function
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  size_t len;
  if (size < sizeof(len)) {
    return 0;
  }
  memcpy(&len, data, sizeof(len));
  data += sizeof(len);
  size -= sizeof(len);

  Buffer(len).Write(data, size);
  return 0;
}
