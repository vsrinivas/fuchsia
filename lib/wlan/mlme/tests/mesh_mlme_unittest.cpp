// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/mesh/mesh_mlme.h>
#include <gtest/gtest.h>

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
        0xd0, 0x00, 0x00, 0x00, // fc, duration
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, // addr1
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, // addr2
        0x03, 0x03, 0x03, 0x03, 0x03, 0x03, // addr3
        0x00, 0x00, // seq ctl
        // Action
        15, // category (self-protected)
        1, // action = Mesh Peering Open
        // Body
        0xaa, 0xbb, // capability info
        1, 1, 0x81, // supported rates
        114, 3, 'f', 'o', 'o', // mesh id
        113, 7, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, // mesh config
        117, 4, 0xb1, 0xb2, 0xb3, 0xb4, // MPM
    };

    ASSERT_EQ(mlme.HandleFramePacket(MakeWlanPacket(frame)), ZX_OK);

    wlan_mlme::MeshPeeringOpenAction msg;
    ASSERT_EQ(device.GetQueuedServiceMsg(fuchsia_wlan_mlme_MLMEIncomingMpOpenActionOrdinal, &msg),
              ZX_OK);

    {
        const uint8_t expected[] = { 'f', 'o', 'o' };
        EXPECT_RANGES_EQ(*msg.common.mesh_id, expected);
    }

    {
        const uint8_t expected[] = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };
        EXPECT_RANGES_EQ(msg.common.peer_sta_address, expected);
    }
}

} // namespace wlan
