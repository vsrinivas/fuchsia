// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "transformer_conformance_utils.h"

#include <iostream>

#include <unittest/unittest.h>

static bool cmp_payload(const uint8_t* actual, size_t actual_size, const uint8_t* expected,
                        size_t expected_size) {
  bool pass = true;
  for (size_t i = 0; i < actual_size && i < expected_size; i++) {
    if (actual[i] != expected[i]) {
      pass = false;
      std::cout << std::dec << "element[" << i << "]: " << std::hex << "actual=0x" << +actual[i]
                << " "
                << "expected=0x" << +expected[i] << "\n";
    }
  }
  if (actual_size != expected_size) {
    pass = false;
    std::cout << std::dec << "element[...]: "
              << "actual.size=" << +actual_size << " "
              << "expected.size=" << +expected_size << "\n";
  }
  return pass;
}

bool check_fidl_transform(fidl_transformation_t transformation, const fidl_type_t* type,
                          const uint8_t* src_bytes, uint32_t src_num_bytes,
                          const uint8_t* expected_bytes, uint32_t expected_num_bytes) {
  BEGIN_HELPER;

  uint8_t actual_dst_bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  uint32_t actual_dst_num_bytes;
  memset(actual_dst_bytes, 0xcc /* poison */, ZX_CHANNEL_MAX_MSG_BYTES);

  const char* error = nullptr;
  zx_status_t status = fidl_transform(transformation, type, src_bytes, src_num_bytes,
                                      actual_dst_bytes, &actual_dst_num_bytes, &error);
  if (error) {
    printf("ERROR: %s\n", error);
  }

  ASSERT_EQ(status, ZX_OK);
  ASSERT_TRUE(
      cmp_payload(actual_dst_bytes, actual_dst_num_bytes, expected_bytes, expected_num_bytes));

  END_HELPER;
}
