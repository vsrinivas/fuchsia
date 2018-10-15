// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/data/domain.h"

#include <fbl/macros.h>
#include <lib/async/cpp/task.h>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller_test.h"
#include "garnet/drivers/bluetooth/lib/testing/test_controller.h"

// This test harness provides test cases for interations between L2CAP, RFCOMM,
// and SocketFactory in integration, as they are implemented by the domain
// object. These exercise a production data plane against raw HCI endpoints.

namespace btlib {
namespace data {
namespace {

using ::btlib::testing::TestController;
using TestingBase = ::btlib::testing::FakeControllerTest<TestController>;

using common::CreateStaticByteBuffer;
using common::LowerBits;
using common::StaticByteBuffer;
using common::UpperBits;

class DATA_DomainTest : public TestingBase {
 public:
  DATA_DomainTest() = default;
  ~DATA_DomainTest() override = default;

 protected:
  void SetUp() override {
    TestingBase::SetUp();
    InitializeACLDataChannel();

    domain_ = Domain::CreateWithDispatcher(transport(), dispatcher());
    domain_->Initialize();

    StartTestDevice();
  }

  void TearDown() override {
    domain_->ShutDown();
    domain_ = nullptr;
    TestingBase::TearDown();
  }

  // TODO(armansito): Move this to the testing library. This should set up
  // expectations on the TestController and not just transmit.
  void EmulateIncomingChannelCreation(hci::ConnectionHandle link_handle,
                                      l2cap::ChannelId src_id,
                                      l2cap::ChannelId dst_id, l2cap::PSM psm) {
    // clang-format off
    test_device()->SendACLDataChannelPacket(CreateStaticByteBuffer(
        // ACL data header (handle: |link_handle|, length: 12 bytes)
        LowerBits(link_handle), UpperBits(link_handle), 0x0c, 0x00,

        // L2CAP B-frame header (length: 8 bytes, channel-id: 0x0001 (ACL sig))
        0x08, 0x00, 0x01, 0x00,

        // Connection Request (ID: 1, length: 4, |psm|, |src_id|)
        0x02, 0x01, 0x04, 0x00,
        LowerBits(psm), UpperBits(psm), LowerBits(src_id), UpperBits(src_id)));

    RunLoopUntilIdle();

    test_device()->SendACLDataChannelPacket(CreateStaticByteBuffer(
        // ACL data header (handle: |link_handle|, length: 16 bytes)
        LowerBits(link_handle), UpperBits(link_handle), 0x10, 0x00,

        // L2CAP B-frame header (length: 12 bytes, channel-id: 0x0001 (ACL sig))
        0x0c, 0x00, 0x01, 0x00,

        // Configuration Request (ID: 6, length: 8, |dst_id|, flags: 0,
        // options: [type: MTU, length: 2, MTU: 1024])
        0x04, 0x06, 0x08, 0x00,
        LowerBits(dst_id), UpperBits(dst_id), 0x00, 0x00,
        0x01, 0x02, 0x00, 0x04));

    test_device()->SendACLDataChannelPacket(CreateStaticByteBuffer(
        // ACL data header (handle: |link_handle|, length: 14 bytes)
        LowerBits(link_handle), UpperBits(link_handle), 0x0e, 0x00,

        // L2CAP B-frame header (length: 10 bytes, channel-id: 0x0001 (ACL sig))
        0x0a, 0x00, 0x01, 0x00,

        // Configuration Response (ID: 1, length: 6, src cid: |dst_id|, flags: 0,
        // result: success)
        0x05, 0x01, 0x06, 0x00,
        LowerBits(dst_id), UpperBits(dst_id), 0x00, 0x00,
        0x00, 0x00));
    // clang-format on

    RunLoopUntilIdle();
  }

  Domain* domain() const { return domain_.get(); }

 private:
  fbl::RefPtr<Domain> domain_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(DATA_DomainTest);
};

TEST_F(DATA_DomainTest, InboundL2capSocket) {
  constexpr l2cap::PSM kPSM = l2cap::kAVDTP;
  constexpr l2cap::ChannelId kLocalId = 0x0040;
  constexpr l2cap::ChannelId kRemoteId = 0x9042;
  constexpr hci::ConnectionHandle kLinkHandle = 0x0001;

  // Register a fake link.
  domain()->AddACLConnection(kLinkHandle, hci::Connection::Role::kMaster, [] {},
                             dispatcher());

  zx::socket sock;
  ASSERT_FALSE(sock);
  auto sock_cb = [&](zx::socket cb_sock, hci::ConnectionHandle handle) {
    EXPECT_EQ(kLinkHandle, handle);
    sock = std::move(cb_sock);
  };

  domain()->RegisterService(kPSM, std::move(sock_cb), dispatcher());
  RunLoopUntilIdle();

  EmulateIncomingChannelCreation(kLinkHandle, kRemoteId, kLocalId, kPSM);
  ASSERT_TRUE(sock);

  // Test basic channel<->socket interaction by verifying that an ACL packet
  // gets routed to the socket.
  test_device()->SendACLDataChannelPacket(CreateStaticByteBuffer(
      // ACL data header (handle: 1, length 8)
      0x01, 0x00, 0x08, 0x00,

      // L2CAP B-frame: (length: 4, channel-id: 0x0040 (kLocalId))
      0x04, 0x00, 0x40, 0x00, 't', 'e', 's', 't'));

  // Run until the packet is written to the socket buffer.
  RunLoopUntilIdle();

  // Allocate a larger buffer than the number of SDU bytes we expect (which is
  // 4).
  StaticByteBuffer<10> socket_bytes;
  size_t bytes_read;
  zx_status_t status = sock.read(0, socket_bytes.mutable_data(),
                                 socket_bytes.size(), &bytes_read);
  EXPECT_EQ(ZX_OK, status);
  ASSERT_EQ(4u, bytes_read);
  EXPECT_EQ("test", socket_bytes.view(0, bytes_read).AsString());
}

// TODO(armansito): Add unit tests for RFCOMM sockets when the Domain class
// has a public API for it.

}  // namespace
}  // namespace data
}  // namespace btlib
