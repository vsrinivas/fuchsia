// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <wlan/mlme/mesh/mesh_mlme.h>

#include <fuchsia/wlan/mlme/c/fidl.h>

#include "mock_device.h"
#include "test_utils.h"

namespace wlan_mlme = ::fuchsia::wlan::mlme;

namespace wlan {

static fbl::unique_ptr<Packet> MakeWlanPacket(Span<const uint8_t> bytes) {
    auto packet = GetWlanPacket(bytes.size());
    memcpy(packet->data(), bytes.data(), bytes.size());
    return packet;
}

TEST(MeshMlme, HandleMpmOpen) {
    MockDevice device;
    MeshMlme mlme(&device);

    const uint8_t frame[] = {
        // Mgmt header
        0xd0, 0x00, 0x00, 0x00,              // fc, duration
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // addr1
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,  // addr2
        0x03, 0x03, 0x03, 0x03, 0x03, 0x03,  // addr3
        0x00, 0x00,                          // seq ctl
        // Action
        15,  // category (self-protected)
        1,   // action = Mesh Peering Open
        // Body
        0xaa, 0xbb,                                        // capability info
        1, 1, 0x81,                                        // supported rates
        114, 3, 'f', 'o', 'o',                             // mesh id
        113, 7, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,  // mesh config
        117, 4, 0xb1, 0xb2, 0xb3, 0xb4,                    // MPM
    };

    ASSERT_EQ(mlme.HandleFramePacket(MakeWlanPacket(frame)), ZX_OK);

    auto msgs = device.GetServiceMsgs<wlan_mlme::MeshPeeringOpenAction>();
    ASSERT_EQ(msgs.size(), 1ULL);

    {
        const uint8_t expected[] = {'f', 'o', 'o'};
        EXPECT_RANGES_EQ(*msgs[0].body()->common.mesh_id, expected);
    }

    {
        const uint8_t expected[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
        EXPECT_RANGES_EQ(msgs[0].body()->common.peer_sta_address, expected);
    }
}

TEST(MeshMlme, DeliverProxiedData) {
    MockDevice device;
    device.state->set_address(common::MacAddr("11:11:11:11:11:11"));
    MeshMlme mlme(&device);

    // Simulate receiving a data frame
    zx_status_t status = mlme.HandleFramePacket(test_utils::MakeWlanPacket({
        // clang-format off
        // Data header
        0x88, 0x03, // fc: qos data, 4-address, no ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr3: mesh da = ra
        0x00, 0x00, // seq ctl
        0x44, 0x44, 0x44, 0x44, 0x44, 0x44, // addr4
        0x00, 0x01, // qos ctl: mesh control present
        // Mesh control
        0x02, // flags: addr56 extension
        0x20, // ttl
        0xaa, 0xbb, 0xcc, 0xdd, // seq
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55, // addr5
        0x66, 0x66, 0x66, 0x66, 0x66, 0x66, // addr6
        // LLC header
        0xaa, 0xaa, 0x03, // dsap ssap ctrl
        0x00, 0x00, 0x00, // oui
        0x12, 0x34, // protocol id
        // Payload
        0xde, 0xad, 0xbe, 0xef,
        // clang-format on
    }));
    EXPECT_EQ(ZX_OK, status);

    auto eth_frames = device.GetEthPackets();
    ASSERT_EQ(1u, eth_frames.size());

    // clang-format off
    const uint8_t expected[] = {
        // Destination = addr5
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
        // Source = addr6
        0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
        // Ethertype = protocol id from the LLC header
        0x12, 0x34,
        // Payload
        0xde, 0xad, 0xbe, 0xef,
    };
    // clang-format on
    EXPECT_RANGES_EQ(expected, eth_frames[0]);
}

}  // namespace wlan
