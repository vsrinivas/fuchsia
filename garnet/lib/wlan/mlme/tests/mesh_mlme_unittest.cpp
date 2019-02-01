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

struct MeshMlmeTest : public ::testing::Test {
    MeshMlmeTest() : mlme(&device) {
        device.state->set_address(common::MacAddr("11:11:11:11:11:11"));
        mlme.Init();
    }

    wlan_mlme::StartResultCodes JoinMesh() {
        wlan_mlme::StartRequest join;
        zx_status_t status =
            mlme.HandleMlmeMsg(MlmeMsg<wlan_mlme::StartRequest>(std::move(join), 123));
        EXPECT_EQ(ZX_OK, status);

        auto msgs = device.GetServiceMsgs<wlan_mlme::StartConfirm>();
        EXPECT_EQ(msgs.size(), 1ULL);
        return msgs[0].body()->result_code;
    }

    wlan_mlme::StopResultCodes LeaveMesh() {
        wlan_mlme::StopRequest leave;
        zx_status_t status =
            mlme.HandleMlmeMsg(MlmeMsg<wlan_mlme::StopRequest>(std::move(leave), 123));
        EXPECT_EQ(ZX_OK, status);

        auto msgs = device.GetServiceMsgs<wlan_mlme::StopConfirm>();
        EXPECT_EQ(msgs.size(), 1ULL);
        return msgs[0].body()->result_code;
    }

    MockDevice device;
    MeshMlme mlme;
};

static fbl::unique_ptr<Packet> MakeWlanPacket(Span<const uint8_t> bytes) {
    auto packet = GetWlanPacket(bytes.size());
    memcpy(packet->data(), bytes.data(), bytes.size());
    return packet;
}

TEST_F(MeshMlmeTest, JoinLeave) {
    EXPECT_EQ(LeaveMesh(), wlan_mlme::StopResultCodes::BSS_ALREADY_STOPPED);
    EXPECT_EQ(JoinMesh(), wlan_mlme::StartResultCodes::SUCCESS);
    EXPECT_TRUE(device.beaconing_enabled);
    EXPECT_EQ(JoinMesh(), wlan_mlme::StartResultCodes::BSS_ALREADY_STARTED_OR_JOINED);
    EXPECT_EQ(LeaveMesh(), wlan_mlme::StopResultCodes::SUCCESS);
    EXPECT_FALSE(device.beaconing_enabled);
    EXPECT_EQ(LeaveMesh(), wlan_mlme::StopResultCodes::BSS_ALREADY_STOPPED);
    EXPECT_EQ(JoinMesh(), wlan_mlme::StartResultCodes::SUCCESS);
    EXPECT_TRUE(device.beaconing_enabled);
}

TEST_F(MeshMlmeTest, HandleMpmOpen) {
    EXPECT_EQ(JoinMesh(), wlan_mlme::StartResultCodes::SUCCESS);

    // clang-format off
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
    // clang-format on

    ASSERT_EQ(mlme.HandleFramePacket(MakeWlanPacket(frame)), ZX_OK);

    auto msgs = device.GetServiceMsgs<wlan_mlme::MeshPeeringOpenAction>();
    ASSERT_EQ(msgs.size(), 1ULL);

    {
        const uint8_t expected[] = {'f', 'o', 'o'};
        EXPECT_RANGES_EQ(msgs[0].body()->common.mesh_id, expected);
    }

    {
        const uint8_t expected[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
        EXPECT_RANGES_EQ(msgs[0].body()->common.peer_sta_address, expected);
    }
}

TEST_F(MeshMlmeTest, DeliverProxiedData) {
    EXPECT_EQ(JoinMesh(), wlan_mlme::StartResultCodes::SUCCESS);

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

TEST_F(MeshMlmeTest, DoNotDeliverWhenNotJoined) {
    auto packet = [] (uint8_t mesh_seq) {
        return test_utils::MakeWlanPacket({
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
            0x00, 0x20, // flags, ttl
            mesh_seq, 0xbb, 0xcc, 0xdd, // seq
            // LLC header
            0xaa, 0xaa, 0x03, // dsap ssap ctrl
            0x00, 0x00, 0x00, // oui
            0x12, 0x34, // protocol id
            // Payload
            0xde, 0xad, 0xbe, 0xef,
            // clang-format on
        });
    };

    // Receive a frame while not joined: expect it to be dropped
    EXPECT_EQ(mlme.HandleFramePacket(packet(1)), ZX_OK);
    EXPECT_TRUE(device.GetEthPackets().empty());

    EXPECT_EQ(JoinMesh(), wlan_mlme::StartResultCodes::SUCCESS);

    // Receive a frame while joined: expect it to be delivered
    EXPECT_EQ(mlme.HandleFramePacket(packet(2)), ZX_OK);
    EXPECT_EQ(device.GetEthPackets().size(), 1u);

    EXPECT_EQ(LeaveMesh(), wlan_mlme::StopResultCodes::SUCCESS);

    // Again, receive a frame while not joined: expect it to be dropped
    EXPECT_EQ(mlme.HandleFramePacket(packet(3)), ZX_OK);
    EXPECT_TRUE(device.GetEthPackets().empty());
}

TEST_F(MeshMlmeTest, HandlePreq) {
    EXPECT_EQ(JoinMesh(), wlan_mlme::StartResultCodes::SUCCESS);

    zx_status_t status = mlme.HandleFramePacket(test_utils::MakeWlanPacket({
        // clang-format off
        // Mgmt header
        0xd0, 0x00, 0x00, 0x00, // fc, duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // addr2
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // addr3
        0x10, 0x00, // seq ctl
        // Action
        13, // category (mesh)
        1, // action = HWMP mesh path selection
        130, 37,
        0x00, // flags: no address extension
        0x03, // hop count
        0x20, // element ttl
        0x04, 0x05, 0x06, 0x07, // path discovery ID
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // originator addr
        0x07, 0x00, 0x00, 0x00, // originator hwmp seqno
        0x05, 0x00, 0x00, 0x00, // lifetime: 5 TU = 5120 mircoseconds
        200, 0, 0, 0, // metric
        1, // target count
        // Target 1
        0x00, // target flags
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // target address
        0x09, 0x00, 0x00, 0x00, // target hwmp seqno
        // clang-format on
    }));
    EXPECT_EQ(ZX_OK, status);

    auto outgoing_packets = device.GetWlanPackets();
    ASSERT_EQ(1u, outgoing_packets.size());

    auto& packet = *outgoing_packets[0].pkt;
    // Simply check that the PREP element is there. hwmp_unittest.cpp tests the actual
    // contents more thoroughly.
    ASSERT_GE(packet.len(), 27u);
    EXPECT_EQ(packet.data()[24], 13);   // mesh action
    EXPECT_EQ(packet.data()[25], 1);    // hwmp
    EXPECT_EQ(packet.data()[26], 131);  // prep element
}

TEST_F(MeshMlmeTest, DeliverDuplicateData) {
    EXPECT_EQ(JoinMesh(), wlan_mlme::StartResultCodes::SUCCESS);

    auto mesh_packet = [](uint8_t addr, uint8_t seq, uint8_t data) {
        // clang-format off
        return std::vector<uint8_t> {
            // Data header
            0x88, 0x03, // fc: qos data, 4-address, no ht ctl
            0x00, 0x00, // duration
            0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
            0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
            0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr3: mesh da = ra
            0x00, 0x00, // seq ctl
            0x44, 0x44, 0x44, 0x44, addr, addr, // addr4
            0x00, 0x01, // qos ctl: mesh control present
            // Mesh control
            0x02, // flags: addr56 extension
            0x20, // ttl
            seq, seq, seq, seq, // seq
            0x55, 0x55, 0x55, 0x55, 0x55, 0x55, // addr5
            0x66, 0x66, 0x66, 0x66, 0x66, 0x66, // addr6
            // LLC header
            0xaa, 0xaa, 0x03, // dsap ssap ctrl
            0x00, 0x00, 0x00, // oui
            0x12, 0x34, // protocol id
            // Payload
            0xde, 0xad, 0xbe, data,
         };
        // clang-format on
    };

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

    // clang-format off
    const uint8_t expected2[] = {
        // Destination = addr5
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
        // Source = addr6
        0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
        // Ethertype = protocol id from the LLC header
        0x12, 0x34,
        // Payload
        0xde, 0xad, 0xbe, 0xff,
    };
    // clang-format on

    // send some non-duplicate packets
    for (uint8_t addr = 1; addr < 5; addr++) {
        for (uint8_t seq = 1; seq < 5; seq++) {
            zx_status_t status =
                mlme.HandleFramePacket(test_utils::MakeWlanPacket(mesh_packet(addr, seq, 0xef)));
            EXPECT_EQ(ZX_OK, status);

            auto eth_frames = device.GetEthPackets();
            ASSERT_EQ(1u, eth_frames.size());

            EXPECT_RANGES_EQ(expected, eth_frames[0]);
        }
    }

    // send some duplicate packets
    for (uint8_t addr = 1; addr < 5; addr++) {
        for (uint8_t seq = 1; seq < 5; seq++) {
            zx_status_t status =
                mlme.HandleFramePacket(test_utils::MakeWlanPacket(mesh_packet(addr, seq, 0xef)));
            EXPECT_EQ(ZX_OK, status);

            auto eth_frames = device.GetEthPackets();
            ASSERT_EQ(0u, eth_frames.size());  // expect 0 packets
        }
    }

    // send some more non-duplicate packets with a different payload
    for (uint8_t addr = 5; addr < 10; addr++) {
        for (uint8_t seq = 0; seq < 5; seq++) {
            zx_status_t status =
                mlme.HandleFramePacket(test_utils::MakeWlanPacket(mesh_packet(addr, seq, 0xff)));
            EXPECT_EQ(ZX_OK, status);

            auto eth_frames = device.GetEthPackets();
            ASSERT_EQ(1u, eth_frames.size());

            EXPECT_RANGES_EQ(expected2, eth_frames[0]);
        }
    }
}

TEST_F(MeshMlmeTest, DataForwarding) {
    EXPECT_EQ(JoinMesh(), wlan_mlme::StartResultCodes::SUCCESS);

    // Receive a PREP to establish a path to 33:33:33:33:33:33 via 22:22:22:22:22:22
    zx_status_t status = mlme.HandleFramePacket(test_utils::MakeWlanPacket({
        // clang-format off
        // Mgmt header
        0xd0, 0x00, 0x00, 0x00, // fc, duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr2
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr3
        0x10, 0x00, // seq ctl
        // Action
        13, // category (mesh)
        1, // action = HWMP mesh path selection
        131, 31, // PREP
        0x00, 0x01, 0x20, // flags, hop count, elem ttl
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // target addr
        0x07, 0x00, 0x00, 0x00, // target hwmp seqno
        0x00, 0x01, 0x00, 0x00, // lifetime
        150, 0x0, 0x0, 0x0, // metric
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // originator addr = self
        0x02, 0x00, 0x00, 0x00, // originator hwmp seqno
        // clang-format on
    }));
    EXPECT_EQ(ZX_OK, status);

    // Receive a data frame originating from 55:55:55:55:55:55 and targeted at 33:33:33:33:33:33,
    // sent to us (11:11:11:11:11:11) by the previous hop (44:44:44:44:44:44)
    status = mlme.HandleFramePacket(test_utils::MakeWlanPacket({
        // clang-format off
        // Data header
        0x88, 0x03, // fc: qos data, 4-address, no ht ctl
        0x00, 0x00, // duration
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr1
        0x44, 0x44, 0x44, 0x44, 0x44, 0x44, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3: mesh da
        0x00, 0x00, // seq ctl
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55, // addr4: mesh sa
        0x00, 0x01, // qos ctl: mesh control present
        // Mesh control
        0x00, 0x20, // flags, ttl
        0xaa, 0xbb, 0xcc, 0xdd, // seq
        // LLC header
        0xaa, 0xaa, 0x03, // dsap ssap ctrl
        0x00, 0x00, 0x00, // oui
        0x12, 0x34, // protocol id
        // Payload
        0xde, 0xad, 0xbe, 0xef,
        // clang-format on
    }));
    EXPECT_EQ(ZX_OK, status);

    auto packets = device.GetWlanPackets();
    ASSERT_EQ(1u, packets.size());

    const uint8_t expected[] = {
        // clang-format off
        // Data header
        0x88, 0x03, // fc: qos data, 4-address, no ht ctl
        0x00, 0x00, // duration
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // addr1: next hop to destination
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // addr2
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, // addr3: mesh da
        0x10, 0x00, // seq ctl: should be filled by us
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55, // addr4: mesh sa
        0x00, 0x01, // qos ctl: mesh control present
        // Mesh control
        0x00, 0x1f, // flags, ttl (decreased by one)
        0xaa, 0xbb, 0xcc, 0xdd, // seq
        // LLC header
        0xaa, 0xaa, 0x03, // dsap ssap ctrl
        0x00, 0x00, 0x00, // oui
        0x12, 0x34, // protocol id
        // Payload
        0xde, 0xad, 0xbe, 0xef,
        // clang-format on
    };
    EXPECT_RANGES_EQ(expected, Span<const uint8_t>(*packets[0].pkt));
}

}  // namespace wlan
