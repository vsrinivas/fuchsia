// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <wlan/mlme/mesh/parse_mp_action.h>

#include "test_utils.h"

namespace wlan_mlme = ::fuchsia::wlan::mlme;

namespace wlan {

TEST(ParseMpOpen, Full) {
  // clang-format off
    const uint8_t data[] = {
        0xaa, 0xbb, // capability info
        1, 8, 0x81, 0x82, 0x83, 0x84, 0x05, 0x06, 0x07, 0x08, // supported rates
        50, 1, 0x09, // ext supported rates
        114, 3, 'f', 'o', 'o', // mesh id
        113, 7, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, // mesh config
        117, 4, 0xb1, 0xb2, 0xb3, 0xb4, // MPM
        45, 26, // ht capabilities
            0xaa, 0xbb, // ht cap info
            0x55, // ampdu params
            0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
            0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, // mcs
            0xdd, 0xee, // ext caps
            0x11, 0x22, 0x33, 0x44, // beamforming
            0x77, // asel
        61, 22, // ht operation
            36, 0x11, 0x22, 0x33, 0x44, 0x55,
            0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
            0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
        191, 12, // vht capabilities
            0xaa, 0xbb, 0xcc, 0xdd,
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
        192, 5, // vht operation
            0xd0, 0xd1, 0xd2, 0xd3, 0xd4
    };
  // clang-format on

  wlan_mlme::MeshPeeringOpenAction action;
  BufferReader reader{data};
  ASSERT_TRUE(ParseMpOpenAction(&reader, &action));

  {
    // Rates are expected to be a concatenation of Supp Rates and Ext Supp Rates
    const uint8_t expected[9] = {0x81, 0x82, 0x83, 0x84, 0x05,
                                 0x06, 0x07, 0x08, 0x09};
    EXPECT_RANGES_EQ(action.common.rates, expected);
  }

  {
    const uint8_t expected[3] = {'f', 'o', 'o'};
    EXPECT_RANGES_EQ(action.common.mesh_id, expected);
  }

  EXPECT_EQ(action.common.mesh_config.active_path_sel_proto_id, 0xa1u);
  EXPECT_EQ(action.common.protocol_id, 0xb2b1u);

  ASSERT_NE(action.common.ht_cap, nullptr);
  EXPECT_EQ(action.common.ht_cap->mcs_set.rx_mcs_set, 0x0706050403020100ul);

  ASSERT_NE(action.common.ht_op, nullptr);
  EXPECT_EQ(action.common.ht_op->basic_mcs_set.rx_mcs_set,
            0xc7c6c5c4c3c2c1c0ul);

  ASSERT_NE(action.common.vht_cap, nullptr);
  EXPECT_EQ(action.common.vht_cap->vht_mcs_nss.rx_max_data_rate, 0x0433);

  ASSERT_NE(action.common.vht_op, nullptr);
  ASSERT_EQ(action.common.vht_op->vht_cbw, 0xd0);
}

TEST(ParseMpOpen, Minimal) {
  // clang-format off
    const uint8_t data[] = {
        0xaa, 0xbb, // capability info
        1, 1, 0x81, // supported rates
        114, 3, 'f', 'o', 'o', // mesh id
        113, 7, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, // mesh config
        117, 4, 0xb1, 0xb2, 0xb3, 0xb4, // MPM
    };
  // clang-format on

  wlan_mlme::MeshPeeringOpenAction action;
  BufferReader reader{data};
  ASSERT_TRUE(ParseMpOpenAction(&reader, &action));

  {
    // Rates are expected to be a concatenation of Supp Rates and Ext Supp Rates
    const uint8_t expected[1] = {0x81};
    EXPECT_RANGES_EQ(action.common.rates, expected);
  }

  {
    const uint8_t expected[3] = {'f', 'o', 'o'};
    EXPECT_RANGES_EQ(action.common.mesh_id, expected);
  }

  EXPECT_EQ(action.common.mesh_config.active_path_sel_proto_id, 0xa1u);
  EXPECT_EQ(action.common.protocol_id, 0xb2b1u);
}

TEST(ParseMpOpen, EmptyMeshId) {
  // clang-format off
    const uint8_t data[] = {
        0xaa, 0xbb, // capability info
        1, 1, 0x81, // supported rates
        114, 0, // mesh id
        113, 7, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, // mesh config
        117, 4, 0xb1, 0xb2, 0xb3, 0xb4, // MPM
    };
  // clang-format on

  wlan_mlme::MeshPeeringOpenAction action;
  BufferReader reader{data};
  ASSERT_TRUE(ParseMpOpenAction(&reader, &action));

  // Make sure that mesh_id is empty.
  EXPECT_TRUE(action.common.mesh_id.empty());
}

TEST(ParseMpOpen, TooShort) {
  const uint8_t data[] = {0xaa};  // too short to hold a CapabilityInfo
  BufferReader reader{data};
  wlan_mlme::MeshPeeringOpenAction action;
  ASSERT_FALSE(ParseMpOpenAction(&reader, &action));
}

TEST(ParseMpOpen, MissingRates) {
  // clang-format off
    const uint8_t data[] = {
        0xaa, 0xbb, // capability info
        114, 3, 'f', 'o', 'o', // mesh id
        113, 7, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, // mesh config
        117, 4, 0xb1, 0xb2, 0xb3, 0xb4, // MPM
    };
  // clang-format on

  wlan_mlme::MeshPeeringOpenAction action;
  BufferReader reader{data};
  ASSERT_FALSE(ParseMpOpenAction(&reader, &action));
}

TEST(ParseMpOpen, MissingMeshId) {
  // clang-format off
    const uint8_t data[] = {
        0xaa, 0xbb, // capability info
        1, 1, 0x81, // supported rates
        113, 7, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, // mesh config
        117, 4, 0xb1, 0xb2, 0xb3, 0xb4, // MPM
    };
  // clang-format on

  wlan_mlme::MeshPeeringOpenAction action;
  BufferReader reader{data};
  ASSERT_FALSE(ParseMpOpenAction(&reader, &action));
}

TEST(ParseMpOpen, MissingMeshConfig) {
  // clang-format off
    const uint8_t data[] = {
        0xaa, 0xbb, // capability info
        1, 1, 0x81, // supported rates
        114, 3, 'f', 'o', 'o', // mesh id
        117, 4, 0xb1, 0xb2, 0xb3, 0xb4, // MPM
    };
  // clang-format on

  wlan_mlme::MeshPeeringOpenAction action;
  BufferReader reader{data};
  ASSERT_FALSE(ParseMpOpenAction(&reader, &action));
}

TEST(ParseMpOpen, MissingMpm) {
  // clang-format off
    const uint8_t data[] = {
        0xaa, 0xbb, // capability info
        1, 1, 0x81, // supported rates
        113, 7, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, // mesh config
        114, 3, 'f', 'o', 'o', // mesh id
    };
  // clang-format on

  wlan_mlme::MeshPeeringOpenAction action;
  BufferReader reader{data};
  ASSERT_FALSE(ParseMpOpenAction(&reader, &action));
}

TEST(ParseMpConfirm, Full) {
  // clang-format off
    const uint8_t data[] = {
        0xaa, 0xbb, // capability info
        0x12, 0x34, // aid
        1, 8, 0x81, 0x82, 0x83, 0x84, 0x05, 0x06, 0x07, 0x08, // supported rates
        50, 1, 0x09, // ext supported rates
        114, 3, 'f', 'o', 'o', // mesh id
        113, 7, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, // mesh config
        117, 6, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, // MPM
        45, 26, // ht capabilities
            0xaa, 0xbb, // ht cap info
            0x55, // ampdu params
            0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
            0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, // mcs
            0xdd, 0xee, // ext caps
            0x11, 0x22, 0x33, 0x44, // beamforming
            0x77, // asel
        61, 22, // ht operation
            36, 0x11, 0x22, 0x33, 0x44, 0x55,
            0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
            0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
        191, 12, // vht capabilities
            0xaa, 0xbb, 0xcc, 0xdd,
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
        192, 5, // vht operation
            0xd0, 0xd1, 0xd2, 0xd3, 0xd4
    };
  // clang-format on

  wlan_mlme::MeshPeeringConfirmAction action;
  BufferReader reader{data};
  ASSERT_TRUE(ParseMpConfirmAction(&reader, &action));

  {
    // Rates are expected to be a concatenation of Supp Rates and Ext Supp Rates
    const uint8_t expected[9] = {0x81, 0x82, 0x83, 0x84, 0x05,
                                 0x06, 0x07, 0x08, 0x09};
    EXPECT_RANGES_EQ(action.common.rates, expected);
  }

  {
    const uint8_t expected[3] = {'f', 'o', 'o'};
    EXPECT_RANGES_EQ(action.common.mesh_id, expected);
  }

  EXPECT_EQ(action.peer_link_id, 0xb6b5);
  EXPECT_EQ(action.aid, 0x3412u);

  EXPECT_EQ(action.common.mesh_config.active_path_sel_proto_id, 0xa1u);
  EXPECT_EQ(action.common.protocol_id, 0xb2b1u);
  EXPECT_EQ(action.common.local_link_id, 0xb4b3);

  ASSERT_NE(action.common.ht_cap, nullptr);
  EXPECT_EQ(action.common.ht_cap->mcs_set.rx_mcs_set, 0x0706050403020100ul);

  ASSERT_NE(action.common.ht_op, nullptr);
  EXPECT_EQ(action.common.ht_op->basic_mcs_set.rx_mcs_set,
            0xc7c6c5c4c3c2c1c0ul);

  ASSERT_NE(action.common.vht_cap, nullptr);
  EXPECT_EQ(action.common.vht_cap->vht_mcs_nss.rx_max_data_rate, 0x0433);

  ASSERT_NE(action.common.vht_op, nullptr);
  ASSERT_EQ(action.common.vht_op->vht_cbw, 0xd0);
}

TEST(ParseMpConfirm, Minimal) {
  // clang-format off
    const uint8_t data[] = {
        0xaa, 0xbb, // capability info
        0x12, 0x34, // AID
        1, 1, 0x81, // supported rates
        114, 3, 'f', 'o', 'o', // mesh id
        113, 7, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, // mesh config
        117, 6, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, // MPM
    };
  // clang-format on

  wlan_mlme::MeshPeeringConfirmAction action;
  BufferReader reader{data};
  ASSERT_TRUE(ParseMpConfirmAction(&reader, &action));

  {
    // Rates are expected to be a concatenation of Supp Rates and Ext Supp Rates
    const uint8_t expected[1] = {0x81};
    EXPECT_RANGES_EQ(action.common.rates, expected);
  }

  {
    const uint8_t expected[3] = {'f', 'o', 'o'};
    EXPECT_RANGES_EQ(action.common.mesh_id, expected);
  }

  EXPECT_EQ(action.aid, 0x3412u);
  EXPECT_EQ(action.peer_link_id, 0xb6b5);

  EXPECT_EQ(action.common.mesh_config.active_path_sel_proto_id, 0xa1u);
  EXPECT_EQ(action.common.protocol_id, 0xb2b1u);
}

TEST(ParseMpConfirm, TooShortForCapabilityInfo) {
  const uint8_t data[] = {0xaa};  // too short to hold a CapabilityInfo
  BufferReader reader{data};
  wlan_mlme::MeshPeeringConfirmAction action;
  ASSERT_FALSE(ParseMpConfirmAction(&reader, &action));
}

TEST(ParseMpConfirm, TooShortForAid) {
  const uint8_t data[] = {0xaa, 0xbb,
                          0xcc};  // too short to hold a CapabilityInfo + AID
  BufferReader reader{data};
  wlan_mlme::MeshPeeringConfirmAction action;
  ASSERT_FALSE(ParseMpConfirmAction(&reader, &action));
}

TEST(ParseMpConfirm, MissingMpm) {
  // clang-format off
    const uint8_t data[] = {
        0xaa, 0xbb, // capability info
        0x12, 0x34, // AID
        1, 1, 0x81, // supported rates
        114, 3, 'f', 'o', 'o', // mesh id
        113, 7, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, // mesh config
    };
  // clang-format on

  wlan_mlme::MeshPeeringConfirmAction action;
  BufferReader reader{data};
  ASSERT_FALSE(ParseMpConfirmAction(&reader, &action));
}

}  // namespace wlan
