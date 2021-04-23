// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuzzer/FuzzedDataProvider.h>

#include "src/devices/bin/driver_manager/manifest_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider provider(data, size);
  auto json_input = provider.ConsumeRemainingBytesAsString();

  json_parser::JSONParser parser;
  rapidjson::Document manifest = parser.ParseFromString(json_input, "fuzzed_input");
  if (parser.HasError()) {
    return 0;
  }
  auto response = ParseDriverManifest(std::move(manifest));
  if (response.is_error()) {
    return 0;
  }
  return 0;
}
