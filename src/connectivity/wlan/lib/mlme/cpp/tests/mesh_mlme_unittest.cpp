// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/mlme/c/fidl.h>
#include <gtest/gtest.h>
#include <wlan/mlme/mesh/mesh_mlme.h>

#include "mock_device.h"
#include "test_utils.h"

namespace wlan_mlme = ::fuchsia::wlan::mlme;
namespace wlan_mesh = ::fuchsia::wlan::mesh;

namespace wlan {

struct MeshMlmeTest : public ::testing::Test {
  MeshMlmeTest() : mlme(&device) {
    device.state->set_address(common::MacAddr("aa:aa:aa:aa:aa:aa"));
    mlme.Init();
  }

  wlan_mlme::StartResultCodes JoinMesh() {
    wlan_mlme::StartRequest join;
    zx_status_t status = mlme.HandleMlmeMsg(
        MlmeMsg<wlan_mlme::StartRequest>(std::move(join), 123));
    EXPECT_EQ(ZX_OK, status);

    auto msgs = device.GetServiceMsgs<wlan_mlme::StartConfirm>();
    EXPECT_EQ(msgs.size(), 1ULL);
    return msgs[0].body()->result_code;
  }

  wlan_mlme::StopResultCodes LeaveMesh() {
    wlan_mlme::StopRequest leave;
    zx_status_t status = mlme.HandleMlmeMsg(
        MlmeMsg<wlan_mlme::StopRequest>(std::move(leave), 123));
    EXPECT_EQ(ZX_OK, status);

    auto msgs = device.GetServiceMsgs<wlan_mlme::StopConfirm>();
    EXPECT_EQ(msgs.size(), 1ULL);
    return msgs[0].body()->result_code;
  }

  auto GetPathTable() {
    wlan_mlme::GetMeshPathTableRequest params;
    zx_status_t status = mlme.HandleMlmeMsg(
        MlmeMsg<wlan_mlme::GetMeshPathTableRequest>(std::move(params), 123));
    EXPECT_EQ(ZX_OK, status);

    return device.GetServiceMsgs<wlan_mesh::MeshPathTable>();
  }

  void EstablishPath(const common::MacAddr& target_addr,
                     const common::MacAddr& next_hop, uint32_t lifetime) {
    // Receive a PREP to establish a path
    zx_status_t status = mlme.HandleFramePacket(test_utils::MakeWlanPacket({
        // clang-format off
            // Mgmt header
            0xd0, 0x00, 0x00, 0x00, // fc, duration
            LIST_MAC_ADDR_BYTES(device.GetState()->address()), // addr1 = self
            LIST_MAC_ADDR_BYTES(next_hop), // addr2
            LIST_MAC_ADDR_BYTES(next_hop), // addr3
            0x10, 0x00, // seq ctl
            // Action
            13, // category (mesh)
            1, // action = HWMP mesh path selection
            131, 31, // PREP
            0x00, 0x01, 0x20, // flags, hop count, elem ttl
            LIST_MAC_ADDR_BYTES(target_addr), // target addr
            LIST_UINT32_BYTES(0u), // target hwmp seqno
            LIST_UINT32_BYTES(lifetime), // lifetime
            LIST_UINT32_BYTES(150), // metric
            LIST_MAC_ADDR_BYTES(device.GetState()->address()), // originator addr = self
            LIST_UINT32_BYTES(2), // originator hwmp seqno
        // clang-format on
    }));
    EXPECT_EQ(ZX_OK, status);
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
  EXPECT_EQ(JoinMesh(),
            wlan_mlme::StartResultCodes::BSS_ALREADY_STARTED_OR_JOINED);
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
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,  // addr1
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20,  // addr2
        0x30, 0x30, 0x30, 0x30, 0x30, 0x30,  // addr3
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
    const uint8_t expected[] = {0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
    EXPECT_RANGES_EQ(msgs[0].body()->common.peer_sta_address, expected);
  }
}

TEST_F(MeshMlmeTest, HandleMpmConfirm) {
  EXPECT_EQ(JoinMesh(), wlan_mlme::StartResultCodes::SUCCESS);

  // clang-format off
    const uint8_t frame[] = {
        // Mgmt header
        0xd0, 0x00, 0x00, 0x00,              // fc, duration
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,  // addr1
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20,  // addr2
        0x30, 0x30, 0x30, 0x30, 0x30, 0x30,  // addr3
        0x00, 0x00,                          // seq ctl
        // Action
        15,  // category (self-protected)
        2,   // action = Mesh Peering Confirm
        // Body
        0xaa, 0xbb,                                        // capability info
        0xcc, 0xdd,                                        // aid
        1, 1, 0x81,                                        // supported rates
        114, 3, 'f', 'o', 'o',                             // mesh id
        113, 7, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,  // mesh config
        117, 6, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,        // MPM
    };
  // clang-format on

  ASSERT_EQ(mlme.HandleFramePacket(MakeWlanPacket(frame)), ZX_OK);

  auto msgs = device.GetServiceMsgs<wlan_mlme::MeshPeeringConfirmAction>();
  ASSERT_EQ(msgs.size(), 1ULL);

  {
    const uint8_t expected[] = {'f', 'o', 'o'};
    EXPECT_RANGES_EQ(msgs[0].body()->common.mesh_id, expected);
  }

  {
    const uint8_t expected[] = {0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
    EXPECT_RANGES_EQ(msgs[0].body()->common.peer_sta_address, expected);
  }
}

TEST_F(MeshMlmeTest, GetPathTable) {
  EXPECT_EQ(JoinMesh(), wlan_mlme::StartResultCodes::SUCCESS);
  auto path_table_msgs = GetPathTable();
  EXPECT_EQ(path_table_msgs.size(), 1ULL);
  EXPECT_EQ(0UL, path_table_msgs[0].body()->paths.size());
}

TEST_F(MeshMlmeTest, DeliverProxiedData) {
  EXPECT_EQ(JoinMesh(), wlan_mlme::StartResultCodes::SUCCESS);

  // Simulate receiving a data frame
  zx_status_t status = mlme.HandleFramePacket(test_utils::MakeWlanPacket({
      // clang-format off
        // Data header
        0x88, 0x03, // fc: qos data, 4-address, no ht ctl
        0x00, 0x00, // duration
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // addr1
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, // addr2
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // addr3: mesh da = ra
        0x00, 0x00, // seq ctl
        0x40, 0x40, 0x40, 0x40, 0x40, 0x40, // addr4
        0x00, 0x01, // qos ctl: mesh control present
        // Mesh control
        0x02, // flags: addr56 extension
        0x20, // ttl
        0xaa, 0xbb, 0xcc, 0xdd, // seq
        0x50, 0x50, 0x50, 0x50, 0x50, 0x50, // addr5
        0x60, 0x60, 0x60, 0x60, 0x60, 0x60, // addr6
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
        0x50, 0x50, 0x50, 0x50, 0x50, 0x50,
        // Source = addr6
        0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
        // Ethertype = protocol id from the LLC header
        0x12, 0x34,
        // Payload
        0xde, 0xad, 0xbe, 0xef,
    };
  // clang-format on
  EXPECT_RANGES_EQ(expected, eth_frames[0]);
}

TEST_F(MeshMlmeTest, DoNotDeliverWhenNotJoined) {
  auto packet = [](uint8_t mesh_seq) {
    return test_utils::MakeWlanPacket({
        // clang-format off
            // Data header
            0x88, 0x03, // fc: qos data, 4-address, no ht ctl
            0x00, 0x00, // duration
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // addr1
            0x20, 0x20, 0x20, 0x20, 0x20, 0x20, // addr2
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // addr3: mesh da = ra
            0x00, 0x00, // seq ctl
            0x40, 0x40, 0x40, 0x40, 0x40, 0x40, // addr4
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
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // addr1
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, // addr2
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, // addr3
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
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // target address
        0x09, 0x00, 0x00, 0x00, // target hwmp seqno
      // clang-format on
  }));
  EXPECT_EQ(ZX_OK, status);

  auto outgoing_packets = device.GetWlanPackets();
  ASSERT_EQ(1u, outgoing_packets.size());

  auto& packet = *outgoing_packets[0].pkt;
  // Simply check that the PREP element is there. hwmp_unittest.cpp tests the
  // actual contents more thoroughly.
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
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // addr1
            0x20, 0x20, 0x20, 0x20, 0x20, 0x20, // addr2
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // addr3: mesh da = ra
            0x00, 0x00, // seq ctl
            0x40, 0x40, 0x40, 0x40, addr, addr, // addr4
            0x00, 0x01, // qos ctl: mesh control present
            // Mesh control
            0x02, // flags: addr56 extension
            0x20, // ttl
            seq, seq, seq, seq, // seq
            0x50, 0x50, 0x50, 0x50, 0x50, 0x50, // addr5
            0x60, 0x60, 0x60, 0x60, 0x60, 0x60, // addr6
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
        0x50, 0x50, 0x50, 0x50, 0x50, 0x50,
        // Source = addr6
        0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
        // Ethertype = protocol id from the LLC header
        0x12, 0x34,
        // Payload
        0xde, 0xad, 0xbe, 0xef,
    };
  // clang-format on

  // clang-format off
    const uint8_t expected2[] = {
        // Destination = addr5
        0x50, 0x50, 0x50, 0x50, 0x50, 0x50,
        // Source = addr6
        0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
        // Ethertype = protocol id from the LLC header
        0x12, 0x34,
        // Payload
        0xde, 0xad, 0xbe, 0xff,
    };
  // clang-format on

  // send some non-duplicate packets
  for (uint8_t addr = 1; addr < 5; addr++) {
    for (uint8_t seq = 1; seq < 5; seq++) {
      zx_status_t status = mlme.HandleFramePacket(
          test_utils::MakeWlanPacket(mesh_packet(addr, seq, 0xef)));
      EXPECT_EQ(ZX_OK, status);

      auto eth_frames = device.GetEthPackets();
      ASSERT_EQ(1u, eth_frames.size());

      EXPECT_RANGES_EQ(expected, eth_frames[0]);
    }
  }

  // send some duplicate packets
  for (uint8_t addr = 1; addr < 5; addr++) {
    for (uint8_t seq = 1; seq < 5; seq++) {
      zx_status_t status = mlme.HandleFramePacket(
          test_utils::MakeWlanPacket(mesh_packet(addr, seq, 0xef)));
      EXPECT_EQ(ZX_OK, status);

      auto eth_frames = device.GetEthPackets();
      ASSERT_EQ(0u, eth_frames.size());  // expect 0 packets
    }
  }

  // send some more non-duplicate packets with a different payload
  for (uint8_t addr = 5; addr < 10; addr++) {
    for (uint8_t seq = 0; seq < 5; seq++) {
      zx_status_t status = mlme.HandleFramePacket(
          test_utils::MakeWlanPacket(mesh_packet(addr, seq, 0xff)));
      EXPECT_EQ(ZX_OK, status);

      auto eth_frames = device.GetEthPackets();
      ASSERT_EQ(1u, eth_frames.size());

      EXPECT_RANGES_EQ(expected2, eth_frames[0]);
    }
  }
}

TEST_F(MeshMlmeTest, DataForwarding) {
  EXPECT_EQ(JoinMesh(), wlan_mlme::StartResultCodes::SUCCESS);

  const common::MacAddr next_hop("20:20:20:20:20:20");
  const common::MacAddr mesh_da("30:30:30:30:30:30");
  const common::MacAddr prev_hop("40:40:40:40:40:40");
  const common::MacAddr mesh_sa("50:50:50:50:50:50");

  // Receive a PREP to establish a path to 'mesh_da' via 'next_hop'
  EstablishPath(mesh_da, next_hop, 256);

  // Receive a data frame originating from 'mesh_sa' and targeted at 'mesh_da',
  // sent to us by 'prev_hop'
  zx_status_t status = mlme.HandleFramePacket(test_utils::MakeWlanPacket({
      // clang-format off
        // Data header
        0x88, 0x03, // fc: qos data, 4-address, no ht ctl
        0x00, 0x00, // duration
        LIST_MAC_ADDR_BYTES(device.GetState()->address()), // addr1
        LIST_MAC_ADDR_BYTES(prev_hop), // addr2
        LIST_MAC_ADDR_BYTES(mesh_da), // addr3
        0x00, 0x00, // seq ctl
        LIST_MAC_ADDR_BYTES(mesh_sa), // addr4
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
        LIST_MAC_ADDR_BYTES(next_hop), // addr1: next hop to destination
        LIST_MAC_ADDR_BYTES(device.GetState()->address()), // addr2 = self
        LIST_MAC_ADDR_BYTES(mesh_da), // addr3
        0x10, 0x00, // seq ctl: should be filled by us
        LIST_MAC_ADDR_BYTES(mesh_sa), // addr4
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

TEST_F(MeshMlmeTest, OutgoingData) {
  const common::MacAddr dest("30:30:30:30:30:30");
  const common::MacAddr next_hop("20:20:20:20:20:20");
  const common::MacAddr src("40:40:40:40:40:40");

  auto expected_data_frame = [](uint8_t index, uint8_t payload) {
    return std::vector<uint8_t>{
        // clang-format off
            // Data header
            0x88, 0x03, // fc: qos data, 4-address, no ht ctl
            0x00, 0x00, // duration
            0x20, 0x20, 0x20, 0x20, 0x20, 0x20, // addr1: next hop to destination
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // addr2: transmitter address (self)
            0x30, 0x30, 0x30, 0x30, 0x30, 0x30, // addr3: mesh da
            static_cast<uint8_t>((index + 1) << 4), 0x00, // seq ctl
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // addr4: mesh sa (self)
            0x00, 0x01, // qos ctl: mesh control present
            // Mesh control
            0x02, 0x20, // flags (addr ext), ttl
            index, 0, 0, 0, // seq
            0x30, 0x30, 0x30, 0x30, 0x30, 0x30, // addr5: da
            0x40, 0x40, 0x40, 0x40, 0x40, 0x40, // addr6: sa
            // LLC header
            0xaa, 0xaa, 0x03, // dsap ssap ctrl
            0x00, 0x00, 0x00, // oui
            0x00, 0x00, // protocol id
            payload,
        // clang-format on
    };
  };

  EXPECT_EQ(JoinMesh(), wlan_mlme::StartResultCodes::SUCCESS);

  EstablishPath(dest, next_hop, 100);

  // Transmit a data frame
  ASSERT_EQ(ZX_OK, mlme.HandleFramePacket(
                       test_utils::MakeEthPacket(dest, src, {'a'})));
  auto packets = device.GetWlanPackets();
  ASSERT_EQ(1u, packets.size());
  EXPECT_RANGES_EQ(expected_data_frame(0, 'a'),
                   Span<const uint8_t>(*packets[0].pkt));

  // Transmit another data frame
  ASSERT_EQ(ZX_OK, mlme.HandleFramePacket(
                       test_utils::MakeEthPacket(dest, src, {'b'})));
  packets = device.GetWlanPackets();
  ASSERT_EQ(1u, packets.size());
  EXPECT_RANGES_EQ(expected_data_frame(1, 'b'),
                   Span<const uint8_t>(*packets[0].pkt));

  // Fast forward well into the future and attempt to transmit yet another data
  // frame
  device.SetTime(zx::time(ZX_SEC(12345)));
  ASSERT_EQ(ZX_OK, mlme.HandleFramePacket(
                       test_utils::MakeEthPacket(dest, src, {'c'})));

  packets = device.GetWlanPackets();
  ASSERT_EQ(2u, packets.size());

  // Expect a PREQ
  const uint8_t expected_preq_frame[] = {
      // clang-format off
        // Mgmt header
        0xd0, 0x00, 0x00, 0x00, // fc, duration
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // addr1
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // addr2
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // addr3
        0x10, 0x00, // seq ctl
        // Action
        13, // category (mesh)
        1, // action = HWMP mesh path selection
        // Preq element
        130, 37,
        0x00, // flags: no address extension
        0x00, // hop count
        0x20, // element ttl
        0x01, 0x00, 0x00, 0x00, // path discovery ID
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, // originator addr
        0x01, 0x00, 0x00, 0x00, // originator hwmp seqno
        0x88, 0x13, 0x00, 0x00, // lifetime (default = 5000 TU)
        0, 0, 0, 0, // metric
        1, // target count
        // Target 1
        0x01, // target flags: target only (default)
        0x30, 0x30, 0x30, 0x30, 0x30, 0x30, // target address
        0x00, 0x00, 0x00, 0x00, // target hwmp seqno
      // clang-format on
  };
  EXPECT_RANGES_EQ(expected_preq_frame, Span<const uint8_t>(*packets[0].pkt));

  // The current implementation is expected to send out the data frame even if
  // the path has expired. This might change in the future if we implement
  // packet buffering.
  EXPECT_RANGES_EQ(expected_data_frame(2, 'c'),
                   Span<const uint8_t>(*packets[1].pkt));
}

TEST_F(MeshMlmeTest, GeneratePerrIfMissingForwardingPath) {
  EXPECT_EQ(JoinMesh(), wlan_mlme::StartResultCodes::SUCCESS);

  // Receive a data frame originating from an external address 60:60:60:60:60:60
  // (proxied by 40:40:40:40:40:40) and targeted at an external address
  // 50:50:50:50:50:50 (proxied by 30:30:30:30:30:30). The frame was sent to us
  // by '20:20:20:20:20:20'.
  zx_status_t status = mlme.HandleFramePacket(test_utils::MakeWlanPacket({
      // clang-format off
        // Data header
        0x88, 0x03, // fc: qos data, 4-address, no ht ctl
        0x00, 0x00, // duration
        LIST_MAC_ADDR_BYTES(device.GetState()->address()), // addr1
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, // addr2
        0x30, 0x30, 0x30, 0x30, 0x30, 0x30, // addr3 (mesh da)
        0x00, 0x00, // seq ctl
        0x40, 0x40, 0x40, 0x40, 0x40, 0x40, // addr4 (mesh sa)
        0x00, 0x01, // qos ctl: mesh control present
        // Mesh control
        0x02, 0x20, // flags: addr56 extension, ttl
        0xaa, 0xbb, 0xcc, 0xdd, // seq
        0x50, 0x50, 0x50, 0x50, 0x50, 0x50, // addr5
        0x60, 0x60, 0x60, 0x60, 0x60, 0x60, // addr6
        // LLC header
        0xaa, 0xaa, 0x03, // dsap ssap ctrl
        0x00, 0x00, 0x00, // oui
        0x12, 0x34, // protocol id
        // Payload
        0xde, 0xad, 0xbe, 0xef,
      // clang-format on
  }));
  EXPECT_EQ(ZX_OK, status);

  // The path to 30:30:30:30:30:30 is missing, so we expect a PERR to be
  // generated

  auto packets = device.GetWlanPackets();
  ASSERT_EQ(1u, packets.size());

  const uint8_t expected_perr_frame[] = {
      // clang-format off
        // Mgmt header
        0xd0, 0x00, 0x00, 0x00, // fc, duration
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, // addr1: the previous hop of the data frame
        LIST_MAC_ADDR_BYTES(device.GetState()->address()), // addr2
        LIST_MAC_ADDR_BYTES(device.GetState()->address()), // addr3
        0x10, 0x00, // seq ctl
        // Action
        13, // category (mesh)
        1, // action = HWMP mesh path selection
        // Perr element
        132, 15,
        0x20, 1, // TTL, number of destinations
        // Perr destination 1
        0x00, // flags: no address extension
        0x30, 0x30, 0x30, 0x30, 0x30, 0x30, // mesh destination to which the path is missing
        0, 0, 0, 0, // hwmp seqno = 0 (unknown)
        62, 0, // reason code = MESH-PATH-ERROR-NO-FORWARDING-INFORMATION
      // clang-format on
  };
  EXPECT_RANGES_EQ(expected_perr_frame, Span<const uint8_t>(*packets[0].pkt));
}

}  // namespace wlan
