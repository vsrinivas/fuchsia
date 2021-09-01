// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/drivers/goldfish_sensor/parser.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  auto* char_data = reinterpret_cast<const char*>(data);
  auto result = goldfish::sensor::ParseSensorReport(char_data, size);
  bool expect_have_name = strnlen(char_data, size) > 0 && char_data[0] != ':';
  bool actual_have_name = !result.name.empty();
  return expect_have_name != actual_have_name;
}
