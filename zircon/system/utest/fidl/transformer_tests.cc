// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/transformer.h>

#include <iostream>

#include <unittest/unittest.h>

#include "generated/transformer_tables.test.h"

namespace {

bool cmp_payload(const uint8_t* actual, size_t actual_size, const uint8_t* expected,
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

uint8_t sandwich1_case1_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich1.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich1.before (padding)

    0xdb, 0xf0, 0xc2, 0x7f,  // UnionSize8Aligned4.tag, i.e. Sandwich1.union
    0x00, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.padding
    0x08, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionSize8Aligned4.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionSize8Aligned4.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich1.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich1.after (padding)

    0x09, 0x0a, 0x0b, 0x0c,  // UnionSize8Aligned4.data, i.e. Sandwich1.union.data
    0x00, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.data (padding)
};

uint8_t sandwich1_case1_old[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich1.before

    0x02, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.tag, i.e. Sandwich1.union
    0x09, 0x0a, 0x0b, 0x0c,  // UnionSize8Aligned4.data

    0x05, 0x06, 0x07, 0x08,  // Sandwich1.after
};

uint8_t sandwich2_case1_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich2.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich2.before (padding)

    0xbf, 0xd3, 0xd1, 0x20,  // UnionSize16Aligned4.tag, i.e. Sandwich2.union
    0x00, 0x00, 0x00, 0x00,  // UnionSize16Aligned4.padding
    0x08, 0x00, 0x00, 0x00,  // UnionSize16Aligned4.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // UnionSize16Aligned4.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionSize16Aligned4.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionSize16Aligned4.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich2.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich2.after (padding)

    0xa0, 0xa1, 0xa2, 0xa3,  // UnionSize16Aligned4.data, i.e. Sandwich2.union.data
    0xa4, 0xa5, 0x00, 0x00,  // UnionSize16Aligned4.data [cont.] and padding
};

uint8_t sandwich2_case1_old[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich2.before

    0x03, 0x00, 0x00, 0x00,  // UnionSize16Aligned4.tag, i.e. Sandwich2.union
    0xa0, 0xa1, 0xa2, 0xa3,  // UnionSize16Aligned4.data
    0xa4, 0xa5, 0x00, 0x00,  // UnionSize16Aligned4.data [cont.] and padding

    0x05, 0x06, 0x07, 0x08,  // Sandwich2.after

    0x00, 0x00, 0x00, 0x00,  // padding for top-level struct
};

uint8_t sandwich3_case1_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich3.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich3.before (padding)

    0x9b, 0x55, 0x04, 0x34,  // UnionSize24Alignement8.tag, i.e. Sandwich2.union
    0x00, 0x00, 0x00, 0x00,  // UnionSize24Alignement8.padding
    0x10, 0x00, 0x00, 0x00,  // UnionSize24Alignement8.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // UnionSize24Alignement8.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionSize24Alignement8.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionSize24Alignement8.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich2.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich2.after (padding)

    0xa0, 0xa1, 0xa2, 0xa3,  // UnionSize24Alignement8.data, i.e Sandwich2.union.data
    0xa4, 0xa5, 0xa6, 0xa7,  // UnionSize24Alignement8.data [cont.]
    0xa8, 0xa9, 0xaa, 0xab,  // UnionSize24Alignement8.data [cont.]
    0xac, 0xad, 0xae, 0xaf,  // UnionSize24Alignement8.data [cont.]
};

uint8_t sandwich3_case1_old[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich3.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich3.before (padding)

    0x03, 0x00, 0x00, 0x00,  // UnionSize24Alignement8.tag, i.e. Sandwich3.union
    0x00, 0x00, 0x00, 0x00,  // UnionSize24Alignement8.tag (padding)
    0xa0, 0xa1, 0xa2, 0xa3,  // UnionSize24Alignement8.data
    0xa4, 0xa5, 0xa6, 0xa7,  // UnionSize24Alignement8.data [cont.]
    0xa8, 0xa9, 0xaa, 0xab,  // UnionSize24Alignement8.data [cont.]
    0xac, 0xad, 0xae, 0xaf,  // UnionSize24Alignement8.data [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich3.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich3.after (padding)
};

uint8_t sandwich4_case1_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich4.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich4.before (padding)

    0x19, 0x10, 0x41, 0x5e,  // UnionSize36Alignment4.tag, i.e. Sandwich4.union
    0x00, 0x00, 0x00, 0x00,  // UnionSize36Alignment4.tag (padding)
    0x20, 0x00, 0x00, 0x00,  // UnionSize36Alignment4.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // UnionSize36Alignment4.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionSize36Alignment4.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionSize36Alignment4.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich4.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich4.after (padding)

    0xa0, 0xa1, 0xa2, 0xa3,  // UnionSize36Alignment4.data, i.e. Sandwich4.union.data
    0xa4, 0xa5, 0xa6, 0xa7,  // UnionSize36Alignment4.data [cont.]
    0xa8, 0xa9, 0xaa, 0xab,  // UnionSize36Alignment4.data [cont.]
    0xac, 0xad, 0xae, 0xaf,  // UnionSize36Alignment4.data [cont.]
    0xb0, 0xb1, 0xb2, 0xb3,  // UnionSize36Alignment4.data [cont.]
    0xb4, 0xb5, 0xb6, 0xb7,  // UnionSize36Alignment4.data [cont.]
    0xb8, 0xb9, 0xba, 0xbb,  // UnionSize36Alignment4.data [cont.]
    0xbc, 0xbd, 0xbe, 0xbf,  // UnionSize36Alignment4.data [cont.]
};

uint8_t sandwich4_case1_old[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich4.before

    0x03, 0x00, 0x00, 0x00,  // UnionSize36Alignment4.tag, i.e. Sandwich2.union
    0xa0, 0xa1, 0xa2, 0xa3,  // UnionSize36Alignment4.data
    0xa4, 0xa5, 0xa6, 0xa7,  // UnionSize36Alignment4.data [cont.]
    0xa8, 0xa9, 0xaa, 0xab,  // UnionSize36Alignment4.data [cont.]
    0xac, 0xad, 0xae, 0xaf,  // UnionSize36Alignment4.data [cont.]
    0xb0, 0xb1, 0xb2, 0xb3,  // UnionSize36Alignment4.data [cont.]
    0xb4, 0xb5, 0xb6, 0xb7,  // UnionSize36Alignment4.data [cont.]
    0xb8, 0xb9, 0xba, 0xbb,  // UnionSize36Alignment4.data [cont.]
    0xbc, 0xbd, 0xbe, 0xbf,  // UnionSize36Alignment4.data [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich4.after

    0x00, 0x00, 0x00, 0x00,  // padding for top-level struct
};

uint8_t sandwich5_case1_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich5.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich5.before (padding)

    0x60, 0xdd, 0xaa, 0x20,  // Sandwich5.UnionOfUnion.ordinal
    0x00, 0x00, 0x00, 0x00,  // Sandwich5.UnionOfUnion.padding
    0x20, 0x00, 0x00, 0x00,  // Sandwich5.UnionOfUnion.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // Sandwich5.UnionOfUnion.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // Sandwich5.UnionOfUnion.env.presence
    0xff, 0xff, 0xff, 0xff,  // Sandwich5.UnionOfUnion.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich5.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich5.after (padding)

    0xdb, 0xf0, 0xc2, 0x7f,  // UnionOfUnion.UnionSize8Aligned4.ordinal
    0x00, 0x00, 0x00, 0x00,  // UnionOfUnion.UnionSize8Aligned4.padding
    0x08, 0x00, 0x00, 0x00,  // UnionOfUnion.UnionSize8Aligned4.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // UnionOfUnion.UnionSize8Aligned4.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionOfUnion.UnionSize8Aligned4.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionOfUnion.UnionSize8Aligned4.env.presence [cont.]

    0x09, 0x0a, 0x0b, 0x0c,  // UnionOfUnion.UnionSize8Aligned4.data
    0x00, 0x00, 0x00, 0x00,  // UnionOfUnion.UnionSize8Aligned4.data (padding)
};

uint8_t sandwich5_case1_old[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich5.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich5.before (padding)

    0x01, 0x00, 0x00, 0x00,  // Sandwich5.UnionOfUnion.tag
    0x00, 0x00, 0x00, 0x00,  // Sandwich5.UnionOfUnion.tag (padding)

    0x02, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.tag, i.e Sandwich5.UnionOfUnion.data
    0x09, 0x0a, 0x0b, 0x0c,  // UnionSize8Aligned4.data
    0x00, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.data (padding)
    0x00, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.data (padding)
    0x00, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.data (padding)
    0x00, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.UnionSize8Aligned4.data (padding)

    0x05, 0x06, 0x07, 0x08,  // Sandwich5.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich5.after (padding)
};

uint8_t sandwich5_case2_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich5.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich5.before (padding)

    0x1f, 0x2d, 0x72, 0x06,  // Sandwich5.UnionOfUnion.ordinal
    0x00, 0x00, 0x00, 0x00,  // Sandwich5.UnionOfUnion.padding
    0x28, 0x00, 0x00, 0x00,  // Sandwich5.UnionOfUnion.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // Sandwich5.UnionOfUnion.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // Sandwich5.UnionOfUnion.env.presence
    0xff, 0xff, 0xff, 0xff,  // Sandwich5.UnionOfUnion.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich5.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich5.after (padding)

    0x9b, 0x55, 0x04, 0x34,  // UnionOfUnion.UnionSize24Alignement8.ordinal
    0x00, 0x00, 0x00, 0x00,  // UnionOfUnion.UnionSize24Alignement8.padding
    0x10, 0x00, 0x00, 0x00,  // UnionOfUnion.UnionSize24Alignement8.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // UnionOfUnion.UnionSize24Alignement8.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionOfUnion.UnionSize24Alignement8.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionOfUnion.UnionSize24Alignement8.env.presence [cont.]

    0xa0, 0xa1, 0xa2, 0xa3,  // UnionOfUnion.UnionSize24Alignement8.data
    0xa4, 0xa5, 0xa6, 0xa7,  // UnionOfUnion.UnionSize24Alignement8.data [cont.]
    0xa8, 0xa9, 0xaa, 0xab,  // UnionOfUnion.UnionSize24Alignement8.data [cont.]
    0xac, 0xad, 0xae, 0xaf,  // UnionOfUnion.UnionSize24Alignement8.data [cont.]
};

uint8_t sandwich5_case2_old[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich5.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich5.before (padding)

    0x03, 0x00, 0x00, 0x00,  // Sandwich5.UnionOfUnion.tag
    0x00, 0x00, 0x00, 0x00,  // Sandwich5.UnionOfUnion.tag (padding)

    0x03, 0x00, 0x00, 0x00,  // UnionSize24Alignement8.tag, i.e Sandwich5.UnionOfUnion.data
    0x00, 0x00, 0x00, 0x00,  // UnionSize24Alignement8.tag (padding)
    0xa0, 0xa1, 0xa2, 0xa3,  // UnionSize24Alignement8.data
    0xa4, 0xa5, 0xa6, 0xa7,  // UnionSize24Alignement8.data [cont.]
    0xa8, 0xa9, 0xaa, 0xab,  // UnionSize24Alignement8.data [cont.]
    0xac, 0xad, 0xae, 0xaf,  // UnionSize24Alignement8.data [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich5.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich5.after (padding)
};

uint8_t sandwich6_case1_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0xad, 0xcc, 0xc3, 0x79,  // UnionWithVector.ordinal (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.ordinal (padding)
    0x18, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)

    0x06, 0x00, 0x00, 0x00,  // vector<uint8>.size, i.e. Sandwich6.union.data
    0x00, 0x00, 0x00, 0x00,  // vector<uint8>.size [cont.]
    0xff, 0xff, 0xff, 0xff,  // vector<uint8>.presence
    0xff, 0xff, 0xff, 0xff,  // vector<uint8>.presence [cont.]

    0xa0, 0xa1, 0xa2, 0xa3,  // vector<uint8>.data
    0xa4, 0xa5, 0x00, 0x00,  // vector<uint8>.data [cont.] + padding
};

uint8_t sandwich6_case1_old[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0x01, 0x00, 0x00, 0x00,  // UnionWithVector.tag (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.tag (padding)
    0x06, 0x00, 0x00, 0x00,  // vector<uint8>.size (start of UnionWithVector.data)
    0x00, 0x00, 0x00, 0x00,  // vector<uint8>.size [cont.]
    0xff, 0xff, 0xff, 0xff,  // vector<uint8>.presence
    0xff, 0xff, 0xff, 0xff,  // vector<uint8>.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)

    0xa0, 0xa1, 0xa2, 0xa3,  // vector<uint8>.data
    0xa4, 0xa5, 0x00, 0x00,  // vector<uint8>.data [cont.] + padding
};

uint8_t sandwich6_case1_absent_vector_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0xad, 0xcc, 0xc3, 0x79,  // UnionWithVector.ordinal (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.ordinal (padding)
    0x10, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)

    0x00, 0x00, 0x00, 0x00,  // vector<uint8>.size, i.e. Sandwich6.union.data
    0x00, 0x00, 0x00, 0x00,  // vector<uint8>.size [cont.]
    0x00, 0x00, 0x00, 0x00,  // vector<uint8>.absence
    0x00, 0x00, 0x00, 0x00,  // vector<uint8>.absence [cont.]
};

uint8_t sandwich6_case1_absent_vector_old[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0x01, 0x00, 0x00, 0x00,  // UnionWithVector.tag (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.tag (padding)
    0x00, 0x00, 0x00, 0x00,  // vector<uint8>.size (start of UnionWithVector.data)
    0x00, 0x00, 0x00, 0x00,  // vector<uint8>.size [cont.]
    0x00, 0x00, 0x00, 0x00,  // vector<uint8>.absence
    0x00, 0x00, 0x00, 0x00,  // vector<uint8>.absence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)
};

uint8_t sandwich6_case2_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0x38, 0x43, 0x31, 0x3b,  // UnionWithVector.ordinal (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.ordinal (padding)
    0x28, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)

    0x15, 0x00, 0x00, 0x00,  // vector<uint8>.size (21), i.e. Sandwich6.union.data
    0x00, 0x00, 0x00, 0x00,  // vector<uint8>.size [cont.]
    0xff, 0xff, 0xff, 0xff,  // vector<uint8>.presence
    0xff, 0xff, 0xff, 0xff,  // vector<uint8>.presence [cont.]

    0x73, 0x6f, 0x66, 0x74,  // vector<uint8>.data
    0x20, 0x6d, 0x69, 0x67,  // vector<uint8>.data [cont.]
    0x72, 0x61, 0x74, 0x69,  // vector<uint8>.data [cont.]
    0x6f, 0x6e, 0x73, 0x20,  // vector<uint8>.data [cont.]
    0x72, 0x6f, 0x63, 0x6b,  // vector<uint8>.data [cont.]
    0x21, 0x00, 0x00, 0x00,  // vector<uint8>.data [cont.] + padding
};

uint8_t sandwich6_case2_old[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0x02, 0x00, 0x00, 0x00,  // UnionWithVector.tag (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.tag (padding)
    0x15, 0x00, 0x00, 0x00,  // vector<uint8>.size (start of UnionWithVector.data)
    0x00, 0x00, 0x00, 0x00,  // vector<uint8>.size [cont.]
    0xff, 0xff, 0xff, 0xff,  // vector<uint8>.presence
    0xff, 0xff, 0xff, 0xff,  // vector<uint8>.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)

    0x73, 0x6f, 0x66, 0x74,  // vector<uint8>.data ("soft migrations rock!")
    0x20, 0x6d, 0x69, 0x67,  // vector<uint8>.data [cont.]
    0x72, 0x61, 0x74, 0x69,  // vector<uint8>.data [cont.]
    0x6f, 0x6e, 0x73, 0x20,  // vector<uint8>.data [cont.]
    0x72, 0x6f, 0x63, 0x6b,  // vector<uint8>.data [cont.]
    0x21, 0x00, 0x00, 0x00,  // vector<uint8>.data [cont.] + padding
};

// TODO(mkember): Verify this example with GIDL. Unsure whether this one needs
// to look like case 6, i.e. due to the alignment of 1 of the struct, there is
// no paddding in between vector elements.
uint8_t sandwich6_case3_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0xdc, 0x3c, 0xc1, 0x4b,  // UnionWithVector.ordinal (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.ordinal (padding)
    0x20, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)

    0x03, 0x00, 0x00, 0x00,  // vector<struct>.size (21), i.e. Sandwich6.union.data
    0x00, 0x00, 0x00, 0x00,  // vector<struct>.size [cont.]
    0xff, 0xff, 0xff, 0xff,  // vector<struct>.presence
    0xff, 0xff, 0xff, 0xff,  // vector<struct>.presence [cont.]

    // TODO(mkember): This section is the one which needs to be verified.
    0x73, 0x6f, 0x66, 0x20,  // StructSize3Alignment1 (element #1 & start of element #2)
    0x6d, 0x69, 0x72, 0x61,  // StructSize3Alignment1 (element #2 [cont.] & start of element #3)
    0x74, 0x00, 0x00, 0x00,  // StructSize3Alignment1 (element #3 [cont.d])
    0x00, 0x00, 0x00, 0x00,  // (padding)
};

uint8_t sandwich6_case3_old[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0x03, 0x00, 0x00, 0x00,  // UnionWithVector.tag (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.tag (padding)
    0x03, 0x00, 0x00, 0x00,  // vector<uint8>.size (start of UnionWithVector.data)
    0x00, 0x00, 0x00, 0x00,  // vector<uint8>.size [cont.]
    0xff, 0xff, 0xff, 0xff,  // vector<uint8>.presence
    0xff, 0xff, 0xff, 0xff,  // vector<uint8>.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)

    0x73, 0x6f, 0x66, 0x20,  // StructSize3Alignment1 (element #1 & start of element #2)
    0x6d, 0x69, 0x72, 0x61,  // StructSize3Alignment1 (element #2 [cont.] & start of element #3)
    0x74, 0x00, 0x00, 0x00,  // StructSize3Alignment1 (element #3 [cont.d])
    0x00, 0x00, 0x00, 0x00,  // (padding)
};

uint8_t sandwich6_case4_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0x3c, 0xaa, 0x08, 0x1d,  // UnionWithVector.ordinal (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.ordinal (padding)
    0x20, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)

    0x03, 0x00, 0x00, 0x00,  // vector<struct>.size, i.e. Sandwich6.union.data
    0x00, 0x00, 0x00, 0x00,  // vector<struct>.size [cont.]
    0xff, 0xff, 0xff, 0xff,  // vector<struct>.presence
    0xff, 0xff, 0xff, 0xff,  // vector<struct>.presence [cont.]

    0x73, 0x6f, 0x66, 0x00,  // StructSize3Alignment2 (start of vector<struct>.data)
    0x20, 0x6d, 0x69, 0x00,  // StructSize3Alignment2 (element #2)
    0x72, 0x61, 0x74, 0x00,  // StructSize3Alignment2 (element #3)
    0x00, 0x00, 0x00, 0x00,  // (padding)
};

uint8_t sandwich6_case4_old[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0x04, 0x00, 0x00, 0x00,  // UnionWithVector.tag (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.tag (padding)
    0x03, 0x00, 0x00, 0x00,  // vector<uint8>.size (start of UnionWithVector.data)
    0x00, 0x00, 0x00, 0x00,  // vector<uint8>.size [cont.]
    0xff, 0xff, 0xff, 0xff,  // vector<uint8>.presence
    0xff, 0xff, 0xff, 0xff,  // vector<uint8>.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)

    0x73, 0x6f, 0x66, 0x00,  // StructSize3Alignment2 (start of vector<struct>.data)
    0x20, 0x6d, 0x69, 0x00,  // StructSize3Alignment2 (element #2)
    0x72, 0x61, 0x74, 0x00,  // StructSize3Alignment2 (element #3)
    0x00, 0x00, 0x00, 0x00,  // (padding)
};

uint8_t sandwich6_case5_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0x76, 0xaa, 0x1e, 0x47,  // UnionWithVector.ordinal (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.ordinal (padding)
    0x20, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_bytes
    0x03, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)

    0x03, 0x00, 0x00, 0x00,  // vector<handle>.size, i.e. Sandwich6.union.data
    0x00, 0x00, 0x00, 0x00,  // vector<handle>.size [cont.]
    0xff, 0xff, 0xff, 0xff,  // vector<handle>.presence
    0xff, 0xff, 0xff, 0xff,  // vector<handle>.presence [cont.]

    0xff, 0xff, 0xff, 0xff,  // vector<handle>.data
    0xff, 0xff, 0xff, 0xff,  // vector<handle>.data
    0xff, 0xff, 0xff, 0xff,  // vector<handle>.data
    0x00, 0x00, 0x00, 0x00,  // vector<handle>.data (padding)
};

uint8_t sandwich6_case5_old[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0x05, 0x00, 0x00, 0x00,  // UnionWithVector.tag (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.tag (padding)
    0x03, 0x00, 0x00, 0x00,  // vector<handle>.size, i.e. Sandwich6.union.data
    0x00, 0x00, 0x00, 0x00,  // vector<handle>.size [cont.]
    0xff, 0xff, 0xff, 0xff,  // vector<handle>.presence
    0xff, 0xff, 0xff, 0xff,  // vector<handle>.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)

    0xff, 0xff, 0xff, 0xff,  // vector<handle>.data
    0xff, 0xff, 0xff, 0xff,  // vector<handle>.data
    0xff, 0xff, 0xff, 0xff,  // vector<handle>.data
    0x00, 0x00, 0x00, 0x00,  // vector<handle>.data (padding)
};

uint8_t sandwich6_case6_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0x10, 0xa8, 0xa0, 0x5e,  // UnionWithVector.ordinal (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.ordinal (padding)
    0x08, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)

    0xa1, 0xa2, 0xa3, 0xa4,  // array<StructSize3Alignment1>:2, i.e. Sandwich6.union.data
    0xa5, 0xa6, 0x00, 0x00,  // array<StructSize3Alignment1>:2
};

uint8_t sandwich6_case6_old[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0x06, 0x00, 0x00, 0x00,  // UnionWithVector.tag (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.tag (padding)
    0xa1, 0xa2, 0xa3, 0xa4,  // array<StructSize3Alignment1>:2, i.e. Sandwich6.union.data
    0xa5, 0xa6, 0x00, 0x00,  // array<StructSize3Alignment1>:2
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.union.data (padding)
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.union.data (padding)

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)
};

uint8_t sandwich6_case7_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0x0d, 0xb7, 0xf8, 0x5c,  // UnionWithVector.ordinal (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.ordinal (padding)
    0x08, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)

    0xa1, 0xa2, 0xa3, 0x00,  // array<StructSize3Alignment2>:2, i.e. Sandwich6.union.data
    0xa4, 0xa5, 0xa6, 0x00,  // array<StructSize3Alignment2>:2
};

uint8_t sandwich6_case7_old[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0x07, 0x00, 0x00, 0x00,  // UnionWithVector.tag (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.tag (padding)
    0xa1, 0xa2, 0xa3, 0x00,  // array<StructSize3Alignment2>:2, i.e. Sandwich6.union.data
    0xa4, 0xa5, 0xa6, 0x00,  // array<StructSize3Alignment2>:2
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.union.data (padding)
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.union.data (padding)

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)
};

uint8_t sandwich6_case8_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0x31, 0x8c, 0x76, 0x2b,  // UnionWithVector.ordinal (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.ordinal (padding)
    0x30, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)

    0x01, 0x00, 0x00, 0x00,  // vector<UnionSize8Aligned4>.size (start of Sandwich6.union.data)
    0x00, 0x00, 0x00, 0x00,  // vector<UnionSize8Aligned4>.size [cont.]
    0xff, 0xff, 0xff, 0xff,  // vector<UnionSize8Aligned4>.presence
    0xff, 0xff, 0xff, 0xff,  // vector<UnionSize8Aligned4>.presence [cont.]

    0xdb, 0xf0, 0xc2, 0x7f,  // UnionSize8Aligned4.ordinal (first element, outer vector)
    0x00, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.ordinal (padding)
    0x08, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionSize8Aligned4.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionSize8Aligned4.env.presence [cont.]

    0x09, 0x0a, 0x0b, 0x0c,  // UnionSize8Aligned4.data, i.e. Sandwich1.union.data
    0x00, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.data (padding)
};

uint8_t sandwich6_case8_old[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich6.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.before (padding)

    0x08, 0x00, 0x00, 0x00,  // UnionWithVector.tag (start of Sandwich6.union)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.tag (padding)
    0x01, 0x00, 0x00, 0x00,  // vector<UnionWithVector>.size (outer vector)
    0x00, 0x00, 0x00, 0x00,  // vector<UnionWithVector>.size [cont.]
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionWithVector.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich6.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich6.after (padding)

    0x02, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.tag, i.e. Sandwich1.union
    0x09, 0x0a, 0x0b, 0x0c,  // UnionSize8Aligned4.data
};

uint8_t sandwich7_case1_v1[] = {
    0x11, 0x12, 0x13, 0x14,  // Sandwich7.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich7.before (padding)
    0xff, 0xff, 0xff, 0xff,  // Sandwich7.opt_sandwich1.presence
    0xff, 0xff, 0xff, 0xff,  // Sandwich7.opt_sandwich1.presence [cont.]
    0x21, 0x22, 0x23, 0x24,  // Sandwich7.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich7.after (padding)

    0x01, 0x02, 0x03, 0x04,  // Sandwich1.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich1.before (padding)  // <--
    0xdb, 0xf0, 0xc2, 0x7f,  // UnionSize8Aligned4.tag, i.e. Sandwich1.union
    0x00, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.padding
    0x08, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionSize8Aligned4.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionSize8Aligned4.presence [cont.]
    0x05, 0x06, 0x07, 0x08,  // Sandwich1.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich1.after (padding)

    0x09, 0x0a, 0x0b, 0x0c,  // UnionSize8Aligned4.data, i.e. Sandwich1.union.data
    0x00, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.data (padding)
};

uint8_t sandwich7_case1_old[] = {
    0x11, 0x12, 0x13, 0x14,  // Sandwich7.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich7.before (padding)
    0xff, 0xff, 0xff, 0xff,  // Sandwich7.opt_sandwich1.presence
    0xff, 0xff, 0xff, 0xff,  // Sandwich7.opt_sandwich1.presence [cont.]
    0x21, 0x22, 0x23, 0x24,  // Sandwich7.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich7.after (padding)

    0x01, 0x02, 0x03, 0x04,  // Sandwich1.before
    0x02, 0x00, 0x00, 0x00,  // UnionSize8Aligned4.tag, i.e. Sandwich1.union
    0x09, 0x0a, 0x0b, 0x0c,  // UnionSize8Aligned4.data
    0x05, 0x06, 0x07, 0x08,  // Sandwich1.after
};

uint8_t sandwich7_case2_v1[] = {
    0x11, 0x12, 0x13, 0x14,  // Sandwich7.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich7.before (padding)
    0x00, 0x00, 0x00, 0x00,  // Sandwich7.opt_sandwich1.preabsentsence
    0x00, 0x00, 0x00, 0x00,  // Sandwich7.opt_sandwich1.absence [cont.]
    0x21, 0x22, 0x23, 0x24,  // Sandwich7.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich7.after (padding)
};

uint8_t sandwich7_case2_old[] = {
    0x11, 0x12, 0x13, 0x14,  // Sandwich7.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich7.before (padding)
    0x00, 0x00, 0x00, 0x00,  // Sandwich7.opt_sandwich1.absence
    0x00, 0x00, 0x00, 0x00,  // Sandwich7.opt_sandwich1.absence [cont.]
    0x21, 0x22, 0x23, 0x24,  // Sandwich7.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich7.after (padding)
};

uint8_t regression1_old_and_v1[] = {
    0x01, 0x00, 0x00, 0x00,  // f1 and padding
    0x02, 0x00, 0x00, 0x00,  // f2 and padding
    0x03, 0x00, 0x04, 0x00,  // f3, f3 padding and f4
    0x00, 0x00, 0x00, 0x00,  // f4 padding
    0x05, 0x00, 0x00, 0x00,  // f5
    0x00, 0x00, 0x00, 0x00,  // f5
    0x06, 0x00, 0x00, 0x00,  // f6 and padding
    0x00, 0x00, 0x00, 0x00,  // f6 padding
};

uint8_t regression2_old_and_v1[] = {
    0x01, 0x00, 0x00, 0x00,  // f1 and padding
    0x02, 0x00, 0x00, 0x00,  // f2 and padding
    0x03, 0x00, 0x04, 0x00,  // f3, f3 padding and f4
    0x00, 0x00, 0x00, 0x00,  // f4 padding
    0x05, 0x00, 0x00, 0x00,  // f5
    0x00, 0x00, 0x00, 0x00,  // f5
    0x06, 0x00, 0x00, 0x00,  // f6 and padding
    0x00, 0x00, 0x00, 0x00,  // f6 padding
    0x07, 0x00, 0x00, 0x00,  // f7 and padding
    0x00, 0x00, 0x00, 0x00,  // f7 padding
};

uint8_t regression3_absent_old_and_v1[] = {
    0x00, 0x00, 0x00, 0x00,  // opt_value.absence
    0x00, 0x00, 0x00, 0x00,  // opt_value.absence [cont.]
};

uint8_t regression3_present_old_and_v1[] = {
    0xFF, 0xFF, 0xFF, 0xFF,  // opt_value.presence
    0xFF, 0xFF, 0xFF, 0xFF,  // opt_value.presence [cont.]
    0x01, 0x00, 0x00, 0x00,  // f1 and padding
    0x02, 0x00, 0x00, 0x00,  // f2 and padding
    0x03, 0x00, 0x04, 0x00,  // f3, f3 padding and f4
    0x00, 0x00, 0x00, 0x00,  // f4 padding
    0x05, 0x00, 0x00, 0x00,  // f5
    0x00, 0x00, 0x00, 0x00,  // f5
    0x06, 0x00, 0x00, 0x00,  // f6 and padding
    0x00, 0x00, 0x00, 0x00,  // f6 padding
    0x07, 0x00, 0x00, 0x00,  // f7 and padding
    0x00, 0x00, 0x00, 0x00,  // f7 padding
};

uint8_t size5alignment1array_old_and_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // a.data[0]
    0x05, 0x06, 0x07, 0x08,  // a.data[0] & a.data[1]
    0x09, 0x0a, 0x0b, 0x0c,  // a.data[1] & a.data[2]
    0x0d, 0x0e, 0x0f, 0x00,  // a.data[2] & padding
};

uint8_t size5alignment4array_old_and_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // a[0].four
    0x05, 0x00, 0x00, 0x00,  // a[0].one + padding
    0x06, 0x07, 0x08, 0x09,  // a[1].four
    0x0a, 0x00, 0x00, 0x00,  // a[1].one + padding
    0x0b, 0x0c, 0x0d, 0x0e,  // a[2].four
    0x0f, 0x00, 0x00, 0x00,  // a[2].one + padding
};

uint8_t size5alignment1vector_old_and_v1[] = {
    0x02, 0x00, 0x00, 0x00,  // v.size
    0x00, 0x00, 0x00, 0x00,  // v.size [cont.]
    0xFF, 0xFF, 0xFF, 0xFF,  // v.presence
    0xFF, 0xFF, 0xFF, 0xFF,  // v.presence [cont.]
    0x01, 0x02, 0x03, 0x04,  // v[0].a.data
    0x05, 0x06, 0x07, 0x08,  // v[0].a.data [cont.] & v[1].a.data
    0x09, 0x0a, 0x00, 0x00,  // v[1].a.data [cont.] & padding
    0x00, 0x00, 0x00, 0x00,  // padding for top-level struct
};

uint8_t size5alignment4vector_old_and_v1[] = {
    0x02, 0x00, 0x00, 0x00,  // v.size
    0x00, 0x00, 0x00, 0x00,  // v.size [cont.]
    0xFF, 0xFF, 0xFF, 0xFF,  // v.presence
    0xFF, 0xFF, 0xFF, 0xFF,  // v.presence [cont.]
    0x01, 0x02, 0x03, 0x04,  // a[0].four
    0x05, 0x00, 0x00, 0x00,  // a[0].one + padding
    0x06, 0x07, 0x08, 0x09,  // a[1].four
    0x0a, 0x00, 0x00, 0x00,  // a[1].one + padding
};

uint8_t table_nofields_v1_and_old[] = {
    0x00, 0x00, 0x00, 0x00,  // Table_NoFields.vector<envelope>.size
    0x00, 0x00, 0x00, 0x00,  // [cont.]
    0xFF, 0xFF, 0xFF, 0xFF,  // Table_NoFields.vector<envelope>.presence
    0xFF, 0xFF, 0xFF, 0xFF,  // [cont.]
};

uint8_t table_tworeservedfields_v1_and_old[] = {
    0x00, 0x00, 0x00, 0x00,  // Table_TwoReservedFields.vector<envelope>.size
    0x00, 0x00, 0x00, 0x00,  // [cont.]
    0xFF, 0xFF, 0xFF, 0xFF,  // Table_TwoReservedFields.vector<envelope>.presence
    0xFF, 0xFF, 0xFF, 0xFF,  // [cont.]
};

uint8_t table_structwithreservedsandwich_v1_and_old[] = {
    0x03, 0x00, 0x00, 0x00,  // Table_StructWithReservedSandwich.vector<envelope>.size
    0x00, 0x00, 0x00, 0x00,  // [cont.]
    0xFF, 0xFF, 0xFF, 0xFF,  // Table_StructWithReservedSandwich.vector<envelope>.presence
    0xFF, 0xFF, 0xFF, 0xFF,  // [cont.]
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[0].num_bytes  0x10
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[0].num_handles
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[0].presence
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[0].presence [cont.]
    0x08, 0x00, 0x00, 0x00,  // vector<envelope>[1].num_bytes  0x20
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[1].num_handles
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[1].presence
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[1].presence [cont.]
    0x08, 0x00, 0x00, 0x00,  // vector<envelope>[2].num_bytes  0x30
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[2].num_handles
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[2].presence
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[2].presence [cont.]
    0x09, 0x0A, 0x0B, 0x00,  // StructSize3Alignment1 data (3 bytes) + padding (1 byte)  0x40
    0x00, 0x00, 0x00, 0x00,  // StructSize3Alignment1 padding [cont.]
    0x19, 0x1A, 0x1B, 0x00,  // StructSize3Alignment1 data (3 bytes) + padding (1 byte)
    0x00, 0x00, 0x00, 0x00,  // StructSize3Alignment1 padding [cont.]
};

uint8_t table_structwithuint32sandwich_v1_and_old[] = {
    0x04, 0x00, 0x00, 0x00,  // Table_StructWithUint32Sandwich.vector<envelope>.size
    0x00, 0x00, 0x00, 0x00,  // [cont.]
    0xFF, 0xFF, 0xFF, 0xFF,  // Table_StructWithUint32Sandwich.vector<envelope>.presence
    0xFF, 0xFF, 0xFF, 0xFF,  // [cont.]
    0x08, 0x00, 0x00, 0x00,  // vector<envelope>[0].num_bytes  0x10
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[0].num_handles
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[0].presence
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[0].presence [cont.]
    0x08, 0x00, 0x00, 0x00,  // vector<envelope>[1].num_bytes  0x20
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[1].num_handles
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[1].presence
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[1].presence [cont.]
    0x08, 0x00, 0x00, 0x00,  // vector<envelope>[2].num_bytes  0x30
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[2].num_handles
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[2].presence
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[2].presence [cont.]
    0x08, 0x00, 0x00, 0x00,  // vector<envelope>[3].num_bytes  0x40
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[3].num_handles
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[3].presence
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[3].presence [cont.]
    0x01, 0x02, 0x03, 0x04,  // i  0x50
    0x00, 0x00, 0x00, 0x00,  // i padding
    0x09, 0x0A, 0x0B, 0x00,  // StructSize3Alignment1 data (3 bytes) + padding (1 byte)
    0x00, 0x00, 0x00, 0x00,  // StructSize3Alignment1 padding [cont.]
    0x19, 0x1A, 0x1B, 0x00,  // StructSize3Alignment1 data (3 bytes) + padding (1 byte)  0x60
    0x00, 0x00, 0x00, 0x00,  // StructSize3Alignment1 padding [cont.]
    0x0A, 0x0B, 0x0C, 0x0D,  // i2
    0x00, 0x00, 0x00, 0x00,  // i2 padding
};

uint8_t table_unionwithvector_reservedsandwich_v1[] = {
    0x02, 0x00, 0x00, 0x00,  // Table_UnionWithVector_ReservedSandwich.vector<envelope>.size
    0x00, 0x00, 0x00, 0x00,  // [cont.]
    0xFF, 0xFF, 0xFF, 0xFF,  // Table_UnionWithVector_ReservedSandwich.vector<envelope>.presence
    0xFF, 0xFF, 0xFF, 0xFF,  // [cont.]
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[0].num_bytes  0x10
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[0].num_handles
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[0].presence
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[0].presence [cont.]
    0x30, 0x00, 0x00, 0x00,  // vector<envelope>[1].num_bytes  0x20
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[1].num_handles
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[1].presence
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[1].presence [cont.]
    0x38, 0x43, 0x31, 0x3B,  // UnionWithVector.xunion.ordinal (string)  0x30
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.xunion.padding
    0x18, 0x00, 0x00, 0x00,  // UnionWithVector.xunion.envelope.size
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.xunion.envelope.size [cont.]
    0xFF, 0xFF, 0xFF, 0xFF,  // UnionWithVector.xunion.envelope.presence  0x40
    0xFF, 0xFF, 0xFF, 0xFF,  // UnionWithVector.xunion.envelope.presence [cont.]
    0x05, 0x00, 0x00, 0x00,  // string.size
    0x00, 0x00, 0x00, 0x00,  // string.size [cont.]
    0xFF, 0xFF, 0xFF, 0xFF,  // string.presence  0x50
    0xFF, 0xFF, 0xFF, 0xFF,  // string.presence [cont.]
    0x68, 0x65, 0x6c, 0x6c,  // "hello"
    0x6f, 0x00, 0x00, 0x00,  // "hello" [cont.] and padding
};

uint8_t table_unionwithvector_reservedsandwich_old[] = {
    0x02, 0x00, 0x00, 0x00,  // Table_UnionWithVector_ReservedSandwich.vector<envelope>.size
    0x00, 0x00, 0x00, 0x00,  // [cont.]
    0xFF, 0xFF, 0xFF, 0xFF,  // Table_UnionWithVector_ReservedSandwich.vector<envelope>.presence
    0xFF, 0xFF, 0xFF, 0xFF,  // [cont.]
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[0].num_bytes  0x10
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[0].num_handles
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[0].presence
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[0].presence [cont.]
    0x20, 0x00, 0x00, 0x00,  // vector<envelope>[1].num_bytes  0x20
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[1].num_handles
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[1].presence
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[1].presence [cont.]
    0x02, 0x00, 0x00, 0x00,  // UnionWithVector.tag (string)  0x30
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.tag padding
    0x05, 0x00, 0x00, 0x00,  // string.size
    0x00, 0x00, 0x00, 0x00,  // string.size [cont.]
    0xFF, 0xFF, 0xFF, 0xFF,  // string.presence  0x40
    0xFF, 0xFF, 0xFF, 0xFF,  // string.presence [cont.]
    0x68, 0x65, 0x6c, 0x6c,  // "hello"  0x50
    0x6f, 0x00, 0x00, 0x00,  // "hello" [cont.] and padding
};

uint8_t table_unionwithvector_structsandwich_v1[] = {
    0x03, 0x00, 0x00, 0x00,  // Table_UnionWithVector_StructSandwich.vector<envelope>.size
    0x00, 0x00, 0x00, 0x00,  // [cont.]
    0xFF, 0xFF, 0xFF, 0xFF,  // Table_UnionWithVector_StructSandwich.vector<envelope>.presence
    0xFF, 0xFF, 0xFF, 0xFF,  // [cont.]
    0x08, 0x00, 0x00, 0x00,  // vector<envelope>[0].num_bytes  0x10
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[0].num_handles
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[0].presence
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[0].presence [cont.]
    0x30, 0x00, 0x00, 0x00,  // vector<envelope>[1].num_bytes  0x20
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[1].num_handles
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[1].presence
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[1].presence [cont.]
    0x08, 0x00, 0x00, 0x00,  // vector<envelope>[2].num_bytes  0x30
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[2].num_handles
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[2].presence
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[2].presence [cont.]
    0x01, 0x02, 0x03, 0x00,  // s1.three_bytes and padding  0x40
    0x00, 0x00, 0x00, 0x00,  // s1 padding
    0x38, 0x43, 0x31, 0x3B,  // UnionWithVector.xunion.ordinal (string)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.xunion.padding
    0x18, 0x00, 0x00, 0x00,  // UnionWithVector.xunion.envelope.size  0x50
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.xunion.envelope.size [cont.]
    0xFF, 0xFF, 0xFF, 0xFF,  // UnionWithVector.xunion.envelope.presence
    0xFF, 0xFF, 0xFF, 0xFF,  // UnionWithVector.xunion.envelope.presence [cont.]
    0x05, 0x00, 0x00, 0x00,  // string.size  0x60
    0x00, 0x00, 0x00, 0x00,  // string.size [cont.]
    0xFF, 0xFF, 0xFF, 0xFF,  // string.presence
    0xFF, 0xFF, 0xFF, 0xFF,  // string.presence [cont.]
    0x68, 0x65, 0x6c, 0x6c,  // "hello"  0x70
    0x6f, 0x00, 0x00, 0x00,  // "hello" [cont.] and padding
    0x04, 0x05, 0x06, 0x00,  // s2.three_bytes and padding  0x80
    0x00, 0x00, 0x00, 0x00,  // s2 padding
};

uint8_t table_unionwithvector_structsandwich_old[] = {
    0x03, 0x00, 0x00, 0x00,  // Table_UnionWithVector_StructSandwich.vector<envelope>.size
    0x00, 0x00, 0x00, 0x00,  // [cont.]
    0xFF, 0xFF, 0xFF, 0xFF,  // Table_UnionWithVector_StructSandwich.vector<envelope>.presence
    0xFF, 0xFF, 0xFF, 0xFF,  // [cont.]
    0x08, 0x00, 0x00, 0x00,  // vector<envelope>[0].num_bytes  0x10
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[0].num_handles
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[0].presence
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[0].presence [cont.]
    0x20, 0x00, 0x00, 0x00,  // vector<envelope>[1].num_bytes  0x20
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[1].num_handles
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[1].presence
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[1].presence [cont.]
    0x08, 0x00, 0x00, 0x00,  // vector<envelope>[2].num_bytes  0x30
    0x00, 0x00, 0x00, 0x00,  // vector<envelope>[2].num_handles
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[2].presence
    0xFF, 0xFF, 0xFF, 0xFF,  // vector<envelope>[2].presence [cont.]
    0x01, 0x02, 0x03, 0x00,  // s1.three_bytes and padding  0x40
    0x00, 0x00, 0x00, 0x00,  // s1 padding
    0x02, 0x00, 0x00, 0x00,  // UnionWithVector.tag (string)
    0x00, 0x00, 0x00, 0x00,  // UnionWithVector.tag padding
    0x05, 0x00, 0x00, 0x00,  // string.size  0x50
    0x00, 0x00, 0x00, 0x00,  // string.size [cont.]
    0xFF, 0xFF, 0xFF, 0xFF,  // string.presence
    0xFF, 0xFF, 0xFF, 0xFF,  // string.presence [cont.]
    0x68, 0x65, 0x6c, 0x6c,  // "hello"  0x60
    0x6f, 0x00, 0x00, 0x00,  // "hello" [cont.] and padding
    0x04, 0x05, 0x06, 0x00,  // s2.three_bytes and padding  0x70
    0x00, 0x00, 0x00, 0x00,  // s2 padding
};

uint8_t xunionwithstruct_old_and_v1[] = {
    0x0B, 0xC4, 0xB0, 0x04,  // XUnionWithStruct.xunion.ordinal
    0x00, 0x00, 0x00, 0x00,  // XUnionWithStruct.xunion.ordinal padding
    0x08, 0x00, 0x00, 0x00,  // XUnionWithStruct.xunion.envelope.num_bytes
    0x00, 0x00, 0x00, 0x00,  // XUnionWithStruct.xunion.envelope.num_handles
    0xFF, 0xFF, 0xFF, 0xFF,  // XUnionWithStruct.xunion.envelope.presence  0x10
    0xFF, 0xFF, 0xFF, 0xFF,  // XUnionWithStruct.xunion.envelope.presence [cont.]
    0x01, 0x02, 0x03, 0x00,  // s and padding
    0x00, 0x00, 0x00, 0x00,  // padding [cont.]
};

uint8_t xunionwithunknownordinal_old_and_v1[] = {
    0xBA, 0x5E, 0xBA, 0x11,  // XUnionWithStruct.xunion.ordinal
    0x00, 0x00, 0x00, 0x00,  // XUnionWithStruct.xunion.ordinal padding
    0x10, 0x00, 0x00, 0x00,  // XUnionWithStruct.xunion.envelope.num_bytes
    0x00, 0x00, 0x00, 0x00,  // XUnionWithStruct.xunion.envelope.num_handles
    0xFF, 0xFF, 0xFF, 0xFF,  // XUnionWithStruct.xunion.envelope.presence  0x10
    0xFF, 0xFF, 0xFF, 0xFF,  // XUnionWithStruct.xunion.envelope.presence [cont.]
    0x01, 0x02, 0x03, 0x04,  // random data
    0x05, 0x06, 0x07, 0x08,  // random data [cont.]
    0x09, 0x0A, 0x0B, 0x0C,  // random data  0x20
    0x0D, 0x0E, 0x0E, 0x0F,  // random data [cont.]
};

bool run_fidl_transform(const fidl_type_t* v1_type, const fidl_type_t* old_type,
                        const uint8_t* v1_bytes, uint32_t v1_num_bytes, const uint8_t* old_bytes,
                        uint32_t old_num_bytes) {
  BEGIN_HELPER;

  {
    uint8_t actual_old_bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    uint32_t actual_old_num_bytes;
    memset(actual_old_bytes, 0xcc /* poison */, ZX_CHANNEL_MAX_MSG_BYTES);

    const char* error = nullptr;
    zx_status_t status =
        fidl_transform(FIDL_TRANSFORMATION_V1_TO_OLD, v1_type, v1_bytes, v1_num_bytes,
                       actual_old_bytes, &actual_old_num_bytes, &error);
    if (error) {
      printf("ERROR: %s\n", error);
    }

    ASSERT_EQ(status, ZX_OK);
    ASSERT_TRUE(cmp_payload(actual_old_bytes, actual_old_num_bytes, old_bytes, old_num_bytes));
  }

  {
    uint8_t actual_v1_bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    uint32_t actual_v1_num_bytes;
    memset(actual_v1_bytes, 0xcc /* poison */, ZX_CHANNEL_MAX_MSG_BYTES);

    const char* error = nullptr;
    zx_status_t status =
        fidl_transform(FIDL_TRANSFORMATION_OLD_TO_V1, old_type, old_bytes, old_num_bytes,
                       actual_v1_bytes, &actual_v1_num_bytes, &error);
    if (error) {
      printf("ERROR: %s\n", error);
    }

    ASSERT_EQ(status, ZX_OK);
    ASSERT_TRUE(cmp_payload(actual_v1_bytes, actual_v1_num_bytes, v1_bytes, v1_num_bytes));
  }

  END_HELPER;
}

bool sandwich1() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Sandwich1Table, &example_Sandwich1Table,
                                 sandwich1_case1_v1, sizeof(sandwich1_case1_v1),
                                 sandwich1_case1_old, sizeof(sandwich1_case1_old)));

  END_TEST;
}

bool sandwich2() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Sandwich2Table, &example_Sandwich2Table,
                                 sandwich2_case1_v1, sizeof(sandwich2_case1_v1),
                                 sandwich2_case1_old, sizeof(sandwich2_case1_old)));

  END_TEST;
}

bool sandwich3() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Sandwich3Table, &example_Sandwich3Table,
                                 sandwich3_case1_v1, sizeof(sandwich3_case1_v1),
                                 sandwich3_case1_old, sizeof(sandwich3_case1_old)));

  END_TEST;
}

bool sandwich4() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Sandwich4Table, &example_Sandwich4Table,
                                 sandwich4_case1_v1, sizeof(sandwich4_case1_v1),
                                 sandwich4_case1_old, sizeof(sandwich4_case1_old)));

  END_TEST;
}

bool sandwich5_case1() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Sandwich5Table, &example_Sandwich5Table,
                                 sandwich5_case1_v1, sizeof(sandwich5_case1_v1),
                                 sandwich5_case1_old, sizeof(sandwich5_case1_old)));

  END_TEST;
}

bool sandwich5_case2() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Sandwich5Table, &example_Sandwich5Table,
                                 sandwich5_case2_v1, sizeof(sandwich5_case2_v1),
                                 sandwich5_case2_old, sizeof(sandwich5_case2_old)));

  END_TEST;
}

bool sandwich6_case1() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Sandwich6Table, &example_Sandwich6Table,
                                 sandwich6_case1_v1, sizeof(sandwich6_case1_v1),
                                 sandwich6_case1_old, sizeof(sandwich6_case1_old)));

  END_TEST;
}

bool sandwich6_case1_absent_vector() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(
      &v1_example_Sandwich6Table, &example_Sandwich6Table, sandwich6_case1_absent_vector_v1,
      sizeof(sandwich6_case1_absent_vector_v1), sandwich6_case1_absent_vector_old,
      sizeof(sandwich6_case1_absent_vector_old)));

  END_TEST;
}

bool sandwich6_case2() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Sandwich6Table, &example_Sandwich6Table,
                                 sandwich6_case2_v1, sizeof(sandwich6_case2_v1),
                                 sandwich6_case2_old, sizeof(sandwich6_case2_old)));

  END_TEST;
}

bool sandwich6_case3() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Sandwich6Table, &example_Sandwich6Table,
                                 sandwich6_case3_v1, sizeof(sandwich6_case3_v1),
                                 sandwich6_case3_old, sizeof(sandwich6_case3_old)));

  END_TEST;
}

bool sandwich6_case4() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Sandwich6Table, &example_Sandwich6Table,
                                 sandwich6_case4_v1, sizeof(sandwich6_case4_v1),
                                 sandwich6_case4_old, sizeof(sandwich6_case4_old)));

  END_TEST;
}

bool sandwich6_case5() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Sandwich6Table, &example_Sandwich6Table,
                                 sandwich6_case5_v1, sizeof(sandwich6_case5_v1),
                                 sandwich6_case5_old, sizeof(sandwich6_case5_old)));

  END_TEST;
}

bool sandwich6_case6() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Sandwich6Table, &example_Sandwich6Table,
                                 sandwich6_case6_v1, sizeof(sandwich6_case6_v1),
                                 sandwich6_case6_old, sizeof(sandwich6_case6_old)));

  END_TEST;
}

bool sandwich6_case7() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Sandwich6Table, &example_Sandwich6Table,
                                 sandwich6_case7_v1, sizeof(sandwich6_case7_v1),
                                 sandwich6_case7_old, sizeof(sandwich6_case7_old)));

  END_TEST;
}

bool sandwich6_case8() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Sandwich6Table, &example_Sandwich6Table,
                                 sandwich6_case8_v1, sizeof(sandwich6_case8_v1),
                                 sandwich6_case8_old, sizeof(sandwich6_case8_old)));

  END_TEST;
}

bool sandwich7_case1() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Sandwich7Table, &example_Sandwich7Table,
                                 sandwich7_case1_v1, sizeof(sandwich7_case1_v1),
                                 sandwich7_case1_old, sizeof(sandwich7_case1_old)));

  END_TEST;
}

bool sandwich7_case2() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Sandwich7Table, &example_Sandwich7Table,
                                 sandwich7_case2_v1, sizeof(sandwich7_case2_v1),
                                 sandwich7_case2_old, sizeof(sandwich7_case2_old)));

  END_TEST;
}

bool regression1() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Regression1Table, &example_Regression1Table,
                                 regression1_old_and_v1, sizeof(regression1_old_and_v1),
                                 regression1_old_and_v1, sizeof(regression1_old_and_v1)));

  END_TEST;
}

bool regression2() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(&v1_example_Regression2Table, &example_Regression2Table,
                                 regression2_old_and_v1, sizeof(regression2_old_and_v1),
                                 regression2_old_and_v1, sizeof(regression2_old_and_v1)));

  END_TEST;
}

bool regression3_absent() {
  BEGIN_TEST;

  ASSERT_TRUE(
      run_fidl_transform(&v1_example_Regression3Table, &example_Regression3Table,
                         regression3_absent_old_and_v1, sizeof(regression3_absent_old_and_v1),
                         regression3_absent_old_and_v1, sizeof(regression3_absent_old_and_v1)));

  END_TEST;
}

bool regression3_present() {
  BEGIN_TEST;

  ASSERT_TRUE(
      run_fidl_transform(&v1_example_Regression3Table, &example_Regression3Table,
                         regression3_present_old_and_v1, sizeof(regression3_present_old_and_v1),
                         regression3_present_old_and_v1, sizeof(regression3_present_old_and_v1)));

  END_TEST;
}

bool size5alignment1array() {
  BEGIN_TEST;

  ASSERT_TRUE(
      run_fidl_transform(&v1_example_Size5Alignment1ArrayTable, &example_Size5Alignment1ArrayTable,
                         size5alignment1array_old_and_v1, sizeof(size5alignment1array_old_and_v1),
                         size5alignment1array_old_and_v1, sizeof(size5alignment1array_old_and_v1)));

  END_TEST;
}

bool size5alignment4array() {
  BEGIN_TEST;

  ASSERT_TRUE(
      run_fidl_transform(&v1_example_Size5Alignment4ArrayTable, &example_Size5Alignment4ArrayTable,
                         size5alignment4array_old_and_v1, sizeof(size5alignment4array_old_and_v1),
                         size5alignment4array_old_and_v1, sizeof(size5alignment4array_old_and_v1)));

  END_TEST;
}

bool size5alignment1vector() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(
      &v1_example_Size5Alignment1VectorTable, &example_Size5Alignment1VectorTable,
      size5alignment1vector_old_and_v1, sizeof(size5alignment1vector_old_and_v1),
      size5alignment1vector_old_and_v1, sizeof(size5alignment1vector_old_and_v1)));

  END_TEST;
}

bool size5alignment4vector() {
  BEGIN_TEST;

  ASSERT_TRUE(run_fidl_transform(
      &v1_example_Size5Alignment4VectorTable, &example_Size5Alignment4VectorTable,
      size5alignment4vector_old_and_v1, sizeof(size5alignment4vector_old_and_v1),
      size5alignment4vector_old_and_v1, sizeof(size5alignment4vector_old_and_v1)));

  END_TEST;
}

// TODO(apang): Tidy up these macros.
#define DO_TEST(old_coding_table, v1_coding_table, old_bytes, v1_bytes)                           \
  BEGIN_TEST;                                                                                     \
  ASSERT_TRUE(run_fidl_transform(&v1_coding_table, &old_coding_table, v1_bytes, sizeof(v1_bytes), \
                                 old_bytes, sizeof(old_bytes)));                                  \
  END_TEST;

#define DO_X_TEST(coding_table, size, old_bytes, v1_bytes)                             \
  {                                                                                    \
    fidl::FidlStructField field(&coding_table, 0u, 0u, &field);                        \
    fidl::FidlCodedStruct coded_struct(&field, 1, size, coding_table.coded_table.name, \
                                       &coded_struct);                                 \
    fidl_type coded_struct_type(coded_struct);                                         \
                                                                                       \
    DO_TEST(coded_struct_type, coded_struct_type, old_bytes, v1_bytes);                \
  }

#define DO_TABLE_TEST(coding_table, old_bytes, v1_bytes) \
  DO_X_TEST(coding_table, 16, old_bytes, v1_bytes)
#define DO_XUNION_TEST(coding_table, old_bytes, v1_bytes) \
  DO_X_TEST(coding_table, 24, old_bytes, v1_bytes)

bool table_nofields() {
  DO_TABLE_TEST(example_Table_NoFieldsTable, table_nofields_v1_and_old, table_nofields_v1_and_old);
}

bool table_tworeservedfields() {
  DO_TABLE_TEST(example_Table_TwoReservedFieldsTable, table_tworeservedfields_v1_and_old,
                table_tworeservedfields_v1_and_old);
}

bool table_structwithreservedsandwich() {
  DO_TABLE_TEST(example_Table_StructWithReservedSandwichTable,
                table_structwithreservedsandwich_v1_and_old,
                table_structwithreservedsandwich_v1_and_old);
}

bool table_structwithuint32sandwich() {
  DO_TABLE_TEST(example_Table_StructWithUint32SandwichTable,
                table_structwithuint32sandwich_v1_and_old,
                table_structwithuint32sandwich_v1_and_old);
}

bool table_unionwithvector_reservedsandwich() {
  DO_TABLE_TEST(example_Table_UnionWithVector_ReservedSandwichTable,
                table_unionwithvector_reservedsandwich_old,
                table_unionwithvector_reservedsandwich_v1);
}

bool table_unionwithvector_structsandwich() {
  DO_TABLE_TEST(example_Table_UnionWithVector_StructSandwichTable,
                table_unionwithvector_structsandwich_old, table_unionwithvector_structsandwich_v1);
}

bool xunionwithstruct() {
  DO_XUNION_TEST(example_XUnionWithStructTable, xunionwithstruct_old_and_v1,
                 xunionwithstruct_old_and_v1);
}

bool xunionwithunknownordinal() {
  DO_XUNION_TEST(example_XUnionWithStructTable, xunionwithunknownordinal_old_and_v1,
                 xunionwithunknownordinal_old_and_v1);
}

}  // namespace

BEGIN_TEST_CASE(transformer_v1_to_old)
RUN_TEST(sandwich1)
RUN_TEST(sandwich2)
RUN_TEST(sandwich3)
RUN_TEST(sandwich4)
RUN_TEST(sandwich5_case1)
RUN_TEST(sandwich5_case2)
RUN_TEST(sandwich6_case1)
RUN_TEST(sandwich6_case1_absent_vector)
RUN_TEST(sandwich6_case2)
RUN_TEST(sandwich6_case3)
RUN_TEST(sandwich6_case4)
RUN_TEST(sandwich6_case5)
RUN_TEST(sandwich6_case6)
RUN_TEST(sandwich6_case7)
RUN_TEST(sandwich6_case8)
RUN_TEST(sandwich7_case1)
RUN_TEST(sandwich7_case2)
RUN_TEST(regression1)
RUN_TEST(regression2)
RUN_TEST(regression3_absent)
RUN_TEST(regression3_present)
RUN_TEST(size5alignment1array)
RUN_TEST(size5alignment4array)
RUN_TEST(size5alignment1vector)
RUN_TEST(size5alignment4vector)
RUN_TEST(table_nofields)
RUN_TEST(table_tworeservedfields)
RUN_TEST(table_structwithreservedsandwich)
RUN_TEST(table_structwithuint32sandwich)
RUN_TEST(table_unionwithvector_reservedsandwich)
RUN_TEST(table_unionwithvector_structsandwich)
RUN_TEST(xunionwithstruct)
RUN_TEST(xunionwithunknownordinal)
END_TEST_CASE(transformer_v1_to_old)
