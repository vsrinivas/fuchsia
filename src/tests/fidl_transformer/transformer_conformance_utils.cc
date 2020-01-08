// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "transformer_conformance_utils.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

static void dump_array(const char* name, const uint8_t* buffer, size_t size) {
  printf("%s = [", name);
  for (size_t i = 0; i < size; i++) {
    if (i % 8 == 0) {
      printf("\n  ");
    }
    printf("0x%.2x, ", buffer[i]);
  }
  printf("\n]\n");
}

static bool cmp_payload(const uint8_t* actual, size_t actual_size, const uint8_t* expected,
                        size_t expected_size) {
  bool pass = true;
  for (size_t i = 0; i < actual_size && i < expected_size; i++) {
    if (actual[i] != expected[i]) {
      pass = false;
      printf("element[%zu]: actual=0x%.2x expected=0x%.2x\n", i, actual[i], expected[i]);
    }
  }
  if (actual_size != expected_size) {
    pass = false;
    printf("element[...]: actual.size=%zu expected.size=%zu\n", actual_size, expected_size);
  }
  if (!pass) {
    dump_array("actual", actual, actual_size);
    dump_array("expected", expected, expected_size);
  }
  return pass;
}

void run_fidl_transform(fidl_transformation_t transformation, const fidl_type_t* type,
                        const uint8_t* src_bytes, uint32_t src_num_bytes) {
  uint8_t actual_dst_bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  uint32_t actual_dst_num_bytes;
  memset(actual_dst_bytes, 0xcc /* poison */, ZX_CHANNEL_MAX_MSG_BYTES);

  fidl_transform(transformation, type, src_bytes, src_num_bytes, actual_dst_bytes,
                 sizeof actual_dst_bytes, &actual_dst_num_bytes, nullptr);
}

bool check_fidl_transform(fidl_transformation_t transformation, const fidl_type_t* type,
                          const uint8_t* src_bytes, uint32_t src_num_bytes,
                          const uint8_t* expected_bytes, uint32_t expected_num_bytes) {
  uint8_t actual_dst_bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  uint32_t actual_dst_num_bytes;
  memset(actual_dst_bytes, 0xcc /* poison */, ZX_CHANNEL_MAX_MSG_BYTES);

  const char* error = nullptr;
  zx_status_t status =
      fidl_transform(transformation, type, src_bytes, src_num_bytes, actual_dst_bytes,
                     sizeof actual_dst_bytes, &actual_dst_num_bytes, &error);
  if (error) {
    printf("ERROR: %s\n", error);
  }

  return (status == ZX_OK) &&
         cmp_payload(actual_dst_bytes, actual_dst_num_bytes, expected_bytes, expected_num_bytes);
}
