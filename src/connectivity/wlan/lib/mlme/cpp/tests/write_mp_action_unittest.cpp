// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <wlan/mlme/mesh/write_mp_action.h>

#include "test_utils.h"

namespace wlan_mlme = ::fuchsia::wlan::mlme;

namespace wlan {

struct Buf {
    uint8_t data[512] = {};
    BufferWriter w{data};
};

wlan_mlme::MeshPeeringCommon FakeCommonFields() {
    wlan_mlme::MeshPeeringCommon ret;
    common::MacAddr("b0:b1:b2:b3:b4:b5").CopyTo(&ret.peer_sta_address);
    ret.protocol_id = 0x2211;
    ret.local_link_id = 0x4433;
    ret.mesh_id = {'f', 'o', 'o'};
    ret.rates = {0x81, 0x82, 0x83, 0x84, 0x05, 0x06, 0x07, 0x08, 0x09};

    ret.mesh_config.active_path_sel_proto_id = 1;
    ret.mesh_config.active_path_sel_metric_id = 2;
    ret.mesh_config.congest_ctrl_method_id = 3;
    ret.mesh_config.sync_method_id = 4;
    ret.mesh_config.auth_proto_id = 5;
    ret.mesh_config.mesh_formation_info = 6;
    ret.mesh_config.mesh_capability = 7;

    return ret;
}

TEST(WriteMpAction, Open) {
    Buf buf;
    auto seq_mgr = NewSequenceManager();

    wlan_mlme::MeshPeeringOpenAction a;
    a.common = FakeCommonFields();

    WriteMpOpenActionFrame(&buf.w,
                           MacHeaderWriter(common::MacAddr("a0:a1:a2:a3:a4:a5"), seq_mgr.get()), a);

    const uint8_t expected[] = {
        // Mgmt header
        0xd0, 0x00, 0x00, 0x00,              // fc, duration
        0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5,  // addr1
        0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5,  // addr2
        0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5,  // addr3
        0x10, 0x00,                          // seq ctl
        // Action
        15,  // category (self-protected)
        1,   // action = Mesh Peering Open
        // Body
        0x20, 0x00,  // capability info. This is currently hard-coded to 0x0020
        1, 8, 0x81, 0x82, 0x83, 0x84, 0x05, 0x06, 0x07, 0x08,  // supported rates
        50, 1, 0x09,                                           // ext supported rates
        114, 3, 'f', 'o', 'o',                                 // mesh id
        113, 7, 1, 2, 3, 4, 5, 6, 7,                           // mesh config
        117, 4, 0x11, 0x22, 0x33, 0x44,                        // MPM
    };
    EXPECT_RANGES_EQ(expected, buf.w.WrittenData());
}

TEST(WriteMpAction, Confirm) {
    Buf buf;
    auto seq_mgr = NewSequenceManager();

    wlan_mlme::MeshPeeringConfirmAction a;
    a.common = FakeCommonFields();
    a.peer_link_id = 0x6655;
    a.aid = 0x8877;

    WriteMpConfirmActionFrame(
        &buf.w, MacHeaderWriter(common::MacAddr("a0:a1:a2:a3:a4:a5"), seq_mgr.get()), a);

    const uint8_t expected[] = {
        // Mgmt header
        0xd0, 0x00, 0x00, 0x00,              // fc, duration
        0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5,  // addr1
        0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5,  // addr2
        0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5,  // addr3
        0x10, 0x00,                          // seq ctl
        // Action
        15,  // category (self-protected)
        2,   // action = Mesh Peering Confirm
        // Body
        0x20, 0x00,  // capability info. This is currently hard-coded to 0x0020
        0x77, 0x88,  // aid
        1, 8, 0x81, 0x82, 0x83, 0x84, 0x05, 0x06, 0x07, 0x08,  // supported rates
        50, 1, 0x09,                                           // ext supported rates
        114, 3, 'f', 'o', 'o',                                 // mesh id
        113, 7, 1, 2, 3, 4, 5, 6, 7,                           // mesh config
        117, 6, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66             // MPM
    };
    EXPECT_RANGES_EQ(expected, buf.w.WrittenData());
}

}  // namespace wlan
