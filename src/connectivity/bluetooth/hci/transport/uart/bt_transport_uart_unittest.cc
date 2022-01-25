// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bt_transport_uart.h"

#include <fuchsia/hardware/serial/c/fidl.h>
#include <fuchsia/hardware/serialimpl/async/cpp/banjo.h>
#include <lib/async/cpp/task.h>
#include <zircon/device/bt-hci.h>

#include <queue>

#include "lib/gtest/test_loop_fixture.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace {

// HCI UART packet indicators
enum BtHciPacketIndicator {
  kHciNone = 0,
  kHciCommand = 1,
  kHciAclData = 2,
  kHciSco = 3,
  kHciEvent = 4,
};

class FakeSerialDevice : public ddk::SerialImplAsyncProtocol<FakeSerialDevice> {
 public:
  explicit FakeSerialDevice(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  serial_impl_async_protocol_t proto() const {
    serial_impl_async_protocol_t proto;
    proto.ctx = const_cast<FakeSerialDevice*>(this);
    proto.ops = const_cast<serial_impl_async_protocol_ops_t*>(&serial_impl_async_protocol_ops_);
    return proto;
  }

  void QueueReadValue(std::vector<uint8_t> buffer) {
    read_rsp_queue_.emplace(std::move(buffer));
    MaybeRespondToRead();
  }

  const std::vector<std::vector<uint8_t>>& writes() const { return writes_; }

  bool canceled() const { return canceled_; }

  bool enabled() const { return enabled_; }

  void set_writes_paused(bool paused) {
    writes_paused_ = paused;
    MaybeRespondToWrite();
  }

  // ddk::SerialImplAsyncProtocol mixins:

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  zx_status_t SerialImplAsyncGetInfo(serial_port_info_t* out_info) {
    out_info->serial_class = fuchsia_hardware_serial_Class_BLUETOOTH_HCI;
    return ZX_OK;
  }

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static,bugprone-easily-swappable-parameters)
  zx_status_t SerialImplAsyncConfig(uint32_t baud_rate, uint32_t flags) { return ZX_OK; }

  zx_status_t SerialImplAsyncEnable(bool enable) {
    enabled_ = enable;
    return ZX_OK;
  }

  void SerialImplAsyncReadAsync(serial_impl_async_read_async_callback callback, void* cookie) {
    // Serial only supports 1 pending read at a time.
    if (!enabled_ || read_req_) {
      callback(cookie, ZX_ERR_IO_REFUSED, /*buf_buffer=*/nullptr, /*buf_size=*/0);
      return;
    }
    read_req_.emplace(ReadRequest{callback, cookie});
    async::PostTask(dispatcher_, fit::bind_member<&FakeSerialDevice::MaybeRespondToRead>(this));
  }

  void SerialImplAsyncWriteAsync(const uint8_t* buf_buffer, size_t buf_size,
                                 serial_impl_async_write_async_callback callback, void* cookie) {
    ASSERT_FALSE(write_req_);
    std::vector<uint8_t> buffer(buf_buffer, buf_buffer + buf_size);
    writes_.emplace_back(std::move(buffer));
    write_req_.emplace(WriteRequest{callback, cookie});
    async::PostTask(dispatcher_, fit::bind_member<&FakeSerialDevice::MaybeRespondToWrite>(this));
  }

  void SerialImplAsyncCancelAll() {
    if (read_req_) {
      read_req_->callback(read_req_->cookie, ZX_ERR_CANCELED, /*buf_buffer=*/nullptr,
                          /*buf_size=*/0);
      read_req_.reset();
    }
    canceled_ = true;
  }

 private:
  struct ReadRequest {
    serial_impl_async_read_async_callback callback;
    void* cookie;
  };

  struct WriteRequest {
    serial_impl_async_write_async_callback callback;
    void* cookie;
  };

  void MaybeRespondToRead() {
    if (read_rsp_queue_.empty() || !read_req_) {
      return;
    }
    std::vector<uint8_t> buffer = std::move(read_rsp_queue_.front());
    read_rsp_queue_.pop();
    // Callback may send new read request synchronously, so clear read_req_.
    ReadRequest req = read_req_.value();
    read_req_.reset();
    req.callback(req.cookie, ZX_OK, buffer.data(), buffer.size());
  }

  void MaybeRespondToWrite() {
    if (writes_paused_) {
      zxlogf(DEBUG, "FakeSerialDevice: writes paused; queueing completion callback");
      return;
    }
    if (!write_req_) {
      return;
    }
    write_req_->callback(write_req_->cookie, ZX_OK);
    write_req_.reset();
  }

  bool enabled_ = false;
  bool canceled_ = false;
  bool writes_paused_ = false;
  async_dispatcher_t* dispatcher_;
  std::optional<ReadRequest> read_req_;
  std::optional<WriteRequest> write_req_;
  std::queue<std::vector<uint8_t>> read_rsp_queue_;
  std::vector<std::vector<uint8_t>> writes_;
};

class BtTransportUartTest : public ::gtest::TestLoopFixture {
 public:
  BtTransportUartTest() : fake_serial_device_(dispatcher()) {}

  void SetUp() override {
    root_device_ = MockDevice::FakeRootParent();
    root_device_->AddProtocol(ZX_PROTOCOL_SERIAL_IMPL_ASYNC, fake_serial_device_.proto().ops,
                              fake_serial_device_.proto().ctx);

    ASSERT_EQ(bt_transport_uart::BtTransportUart::Create(root_device_.get(), dispatcher()), ZX_OK);
    ASSERT_EQ(1u, root_dev()->child_count());
    ASSERT_TRUE(dut());
    EXPECT_TRUE(fake_serial_device_.enabled());

    // TODO(fxb/91487): Due to Mock DDK limitations, we need to add the BT_HCI protocol to the
    // BtTransportUart MockDevice so that BtHciProtocolClient (and device_get_protocol) work.
    bt_hci_protocol_t proto;
    dut()->GetDeviceContext<bt_transport_uart::BtTransportUart>()->DdkGetProtocol(
        ZX_PROTOCOL_BT_HCI, &proto);
    dut()->AddProtocol(ZX_PROTOCOL_BT_HCI, proto.ops, proto.ctx);
  }

  void TearDown() override {
    RunLoopUntilIdle();
    dut()->UnbindOp();
    EXPECT_EQ(dut()->WaitUntilUnbindReplyCalled(), ZX_OK);
    EXPECT_TRUE(fake_serial_device_.canceled());
    dut()->ReleaseOp();
  }

  MockDevice* root_dev() const { return root_device_.get(); }

  MockDevice* dut() const { return root_device_->GetLatestChild(); }

  FakeSerialDevice* serial() { return &fake_serial_device_; }

 private:
  std::shared_ptr<MockDevice> root_device_;
  FakeSerialDevice fake_serial_device_;
};

// Test fixture that opens all channels and has helpers for reading/writing data.
class BtTransportUartHciProtocolTest : public BtTransportUartTest {
 public:
  void SetUp() override {
    BtTransportUartTest::SetUp();

    ddk::BtHciProtocolClient client(static_cast<zx_device_t*>(dut()));
    ASSERT_TRUE(client.is_valid());

    zx::channel cmd_chan_driver_end;
    ASSERT_EQ(zx::channel::create(/*flags=*/0, &cmd_chan_, &cmd_chan_driver_end), ZX_OK);
    ASSERT_EQ(client.OpenCommandChannel(std::move(cmd_chan_driver_end)), ZX_OK);

    // Configure wait for readable signal on command channel.
    cmd_chan_readable_wait_.set_object(cmd_chan_.get());
    zx_status_t wait_begin_status = cmd_chan_readable_wait_.Begin(dispatcher());
    ASSERT_EQ(wait_begin_status, ZX_OK) << zx_status_get_string(wait_begin_status);

    zx::channel acl_chan_driver_end;
    ASSERT_EQ(zx::channel::create(/*flags=*/0, &acl_chan_, &acl_chan_driver_end), ZX_OK);
    ASSERT_EQ(client.OpenAclDataChannel(std::move(acl_chan_driver_end)), ZX_OK);

    // Configure wait for readable signal on ACL channel.
    acl_chan_readable_wait_.set_object(acl_chan_.get());
    wait_begin_status = acl_chan_readable_wait_.Begin(dispatcher());
    ASSERT_EQ(wait_begin_status, ZX_OK) << zx_status_get_string(wait_begin_status);

    zx::channel snoop_chan_driver_end;
    ZX_ASSERT(zx::channel::create(/*flags=*/0, &snoop_chan_, &snoop_chan_driver_end) == ZX_OK);
    ASSERT_EQ(client.OpenSnoopChannel(std::move(snoop_chan_driver_end)), ZX_OK);

    // Configure wait for readable signal on snoop channel.
    snoop_chan_readable_wait_.set_object(snoop_chan_.get());
    wait_begin_status = snoop_chan_readable_wait_.Begin(dispatcher());
    ASSERT_EQ(wait_begin_status, ZX_OK) << zx_status_get_string(wait_begin_status);
  }

  void TearDown() override {
    cmd_chan_readable_wait_.Cancel();
    cmd_chan_.reset();

    acl_chan_readable_wait_.Cancel();
    acl_chan_.reset();

    snoop_chan_readable_wait_.Cancel();
    snoop_chan_.reset();

    BtTransportUartTest::TearDown();
  }

  const std::vector<std::vector<uint8_t>>& hci_events() const { return cmd_chan_received_packets_; }

  const std::vector<std::vector<uint8_t>>& snoop_packets() const {
    return snoop_chan_received_packets_;
  }

  const std::vector<std::vector<uint8_t>>& received_acl_packets() const {
    return acl_chan_received_packets_;
  }

  zx::channel* cmd_chan() { return &cmd_chan_; }

  zx::channel* acl_chan() { return &acl_chan_; }

 private:
  // This method is shared by the waits for all channels. |wait| is used to differentiate which wait
  // called the method.
  void OnChannelReady(async_dispatcher_t*, async::WaitBase* wait, zx_status_t status,
                      const zx_packet_signal_t* signal) {
    ASSERT_EQ(status, ZX_OK);
    ASSERT_TRUE(signal->observed & ZX_CHANNEL_READABLE);

    zx::unowned_channel chan;
    if (wait == &cmd_chan_readable_wait_) {
      chan = zx::unowned_channel(cmd_chan_);
    } else if (wait == &snoop_chan_readable_wait_) {
      chan = zx::unowned_channel(snoop_chan_);
    } else if (wait == &acl_chan_readable_wait_) {
      chan = zx::unowned_channel(acl_chan_);
    } else {
      ADD_FAILURE() << "unexpected channel in OnChannelReady";
      return;
    }

    for (size_t count = 0; count < signal->count; count++) {
      // Make byte buffer arbitrarily large to hold test packets.
      std::vector<uint8_t> bytes(255);
      uint32_t actual_bytes;
      zx_status_t read_status = chan->read(
          /*flags=*/0, bytes.data(), /*handles=*/nullptr, static_cast<uint32_t>(bytes.size()),
          /*num_handles=*/0, &actual_bytes, /*actual_handles=*/nullptr);
      ASSERT_EQ(read_status, ZX_OK);
      bytes.resize(actual_bytes);

      if (wait == &cmd_chan_readable_wait_) {
        cmd_chan_received_packets_.push_back(std::move(bytes));
      } else if (wait == &snoop_chan_readable_wait_) {
        snoop_chan_received_packets_.push_back(std::move(bytes));
      } else if (wait == &acl_chan_readable_wait_) {
        acl_chan_received_packets_.push_back(std::move(bytes));
      } else {
        ADD_FAILURE();
        return;
      }
    }

    // The wait needs to be restarted.
    zx_status_t wait_begin_status = wait->Begin(dispatcher());
    ASSERT_EQ(wait_begin_status, ZX_OK) << zx_status_get_string(wait_begin_status);
  }

  zx::channel cmd_chan_;
  zx::channel acl_chan_;
  zx::channel snoop_chan_;

  async::WaitMethod<BtTransportUartHciProtocolTest, &BtTransportUartHciProtocolTest::OnChannelReady>
      cmd_chan_readable_wait_{this, zx_handle_t(), ZX_CHANNEL_READABLE};
  async::WaitMethod<BtTransportUartHciProtocolTest, &BtTransportUartHciProtocolTest::OnChannelReady>
      snoop_chan_readable_wait_{this, zx_handle_t(), ZX_CHANNEL_READABLE};
  async::WaitMethod<BtTransportUartHciProtocolTest, &BtTransportUartHciProtocolTest::OnChannelReady>
      acl_chan_readable_wait_{this, zx_handle_t(), ZX_CHANNEL_READABLE};

  std::vector<std::vector<uint8_t>> cmd_chan_received_packets_;
  std::vector<std::vector<uint8_t>> snoop_chan_received_packets_;
  std::vector<std::vector<uint8_t>> acl_chan_received_packets_;
};

TEST_F(BtTransportUartTest, Lifecycle) {}

TEST_F(BtTransportUartHciProtocolTest, SendAclPackets) {
  const uint8_t kNumPackets = 25;
  for (uint8_t i = 0; i < kNumPackets; i++) {
    const std::vector<uint8_t> kAclPacket = {i};
    zx_status_t write_status =
        acl_chan()->write(/*flags=*/0, kAclPacket.data(), static_cast<uint32_t>(kAclPacket.size()),
                          /*handles=*/nullptr,
                          /*num_handles=*/0);
    ASSERT_EQ(write_status, ZX_OK);
  }
  // Allow ACL packets to be processed and sent to the serial device.
  RunLoopUntilIdle();

  const std::vector<std::vector<uint8_t>>& packets = serial()->writes();
  ASSERT_EQ(packets.size(), kNumPackets);
  for (uint8_t i = 0; i < kNumPackets; i++) {
    // A packet indicator should be prepended.
    std::vector<uint8_t> expected = {BtHciPacketIndicator::kHciAclData, i};
    EXPECT_EQ(packets[i], expected);
  }

  ASSERT_EQ(snoop_packets().size(), kNumPackets);
  for (uint8_t i = 0; i < kNumPackets; i++) {
    // Snoop packets should have a snoop packet flag prepended (NOT a UART packet indicator).
    const std::vector<uint8_t> kExpectedSnoopPacket = {BT_HCI_SNOOP_TYPE_ACL,  // Snoop packet flag
                                                       i};
    EXPECT_EQ(snoop_packets()[i], kExpectedSnoopPacket);
  }
}

TEST_F(BtTransportUartHciProtocolTest, AclReadableSignalIgnoredUntilFirstWriteCompletes) {
  // Delay completion of first write.
  serial()->set_writes_paused(true);

  const uint8_t kNumPackets = 2;
  for (uint8_t i = 0; i < kNumPackets; i++) {
    const std::vector<uint8_t> kAclPacket = {i};
    zx_status_t write_status =
        acl_chan()->write(/*flags=*/0, kAclPacket.data(), static_cast<uint32_t>(kAclPacket.size()),
                          /*handles=*/nullptr,
                          /*num_handles=*/0);
    ASSERT_EQ(write_status, ZX_OK);
  }
  RunLoopUntilIdle();

  // Call the first packet's completion callback. This should resume waiting for signals.
  serial()->set_writes_paused(false);
  // Wait for the readable signal to be processed.
  RunLoopUntilIdle();

  const std::vector<std::vector<uint8_t>>& packets = serial()->writes();
  ASSERT_EQ(packets.size(), kNumPackets);
  for (uint8_t i = 0; i < kNumPackets; i++) {
    // A packet indicator should be prepended.
    std::vector<uint8_t> expected = {BtHciPacketIndicator::kHciAclData, i};
    EXPECT_EQ(packets[i], expected);
  }
}

TEST_F(BtTransportUartHciProtocolTest, ReceiveAclPacketsIn2Parts) {
  const std::vector<uint8_t> kSnoopAclBuffer = {
      BT_HCI_SNOOP_TYPE_ACL | BT_HCI_SNOOP_FLAG_RECV,  // Snoop packet flag
      0x00,
      0x00,  // arbitrary header fields
      0x02,
      0x00,  // 2-byte length in little endian
      0x01,
      0x02,  // arbitrary payload
  };
  std::vector<uint8_t> kSerialAclBuffer = kSnoopAclBuffer;
  kSerialAclBuffer[0] = BtHciPacketIndicator::kHciAclData;
  const std::vector<uint8_t> kAclBuffer(kSnoopAclBuffer.begin() + 1, kSnoopAclBuffer.end());
  // Split the packet length field in half to test corner case.
  const std::vector<uint8_t> kPart1(kSerialAclBuffer.begin(), kSerialAclBuffer.begin() + 4);
  const std::vector<uint8_t> kPart2(kSerialAclBuffer.begin() + 4, kSerialAclBuffer.end());

  const int kNumPackets = 20;
  for (int i = 0; i < kNumPackets; i++) {
    serial()->QueueReadValue(kPart1);
    serial()->QueueReadValue(kPart2);
    RunLoopUntilIdle();
  }

  ASSERT_EQ(received_acl_packets().size(), static_cast<size_t>(kNumPackets));
  for (const std::vector<uint8_t>& packet : received_acl_packets()) {
    EXPECT_EQ(packet.size(), kAclBuffer.size());
    EXPECT_EQ(packet, kAclBuffer);
  }

  RunLoopUntilIdle();
  ASSERT_EQ(snoop_packets().size(), static_cast<size_t>(kNumPackets));
  for (const std::vector<uint8_t>& packet : snoop_packets()) {
    EXPECT_EQ(packet, kSnoopAclBuffer);
  }
}

TEST_F(BtTransportUartHciProtocolTest, SendHciCommands) {
  const std::vector<uint8_t> kSnoopCmd0 = {
      BT_HCI_SNOOP_TYPE_CMD,  // Snoop packet flag
      0x00,                   // arbitrary payload
  };
  const std::vector<uint8_t> kCmd0(kSnoopCmd0.begin() + 1, kSnoopCmd0.end());
  const std::vector<uint8_t> kUartCmd0 = {
      BtHciPacketIndicator::kHciCommand,  // UART packet indicator
      0x00,                               // arbitrary payload
  };
  zx_status_t write_status =
      cmd_chan()->write(/*flags=*/0, kCmd0.data(), static_cast<uint32_t>(kCmd0.size()),
                        /*handles=*/nullptr,
                        /*num_handles=*/0);
  EXPECT_EQ(write_status, ZX_OK);
  RunLoopUntilIdle();
  EXPECT_EQ(serial()->writes().size(), 1u);

  const std::vector<uint8_t> kSnoopCmd1 = {
      BT_HCI_SNOOP_TYPE_CMD,  // Snoop packet flag
      0x01,                   // arbitrary payload
  };
  const std::vector<uint8_t> kCmd1(kSnoopCmd1.begin() + 1, kSnoopCmd1.end());
  const std::vector<uint8_t> kUartCmd1 = {
      BtHciPacketIndicator::kHciCommand,  // UART packet indicator
      0x01,                               // arbitrary payload
  };
  write_status = cmd_chan()->write(/*flags=*/0, kCmd1.data(), static_cast<uint32_t>(kCmd1.size()),
                                   /*handles=*/nullptr,
                                   /*num_handles=*/0);
  EXPECT_EQ(write_status, ZX_OK);
  RunLoopUntilIdle();

  const std::vector<std::vector<uint8_t>>& packets = serial()->writes();
  ASSERT_EQ(packets.size(), 2u);
  EXPECT_EQ(packets[0], kUartCmd0);
  EXPECT_EQ(packets[1], kUartCmd1);

  RunLoopUntilIdle();
  ASSERT_EQ(snoop_packets().size(), 2u);
  EXPECT_EQ(snoop_packets()[0], kSnoopCmd0);
  EXPECT_EQ(snoop_packets()[1], kSnoopCmd1);
}

TEST_F(BtTransportUartHciProtocolTest, CommandReadableSignalIgnoredUntilFirstWriteCompletes) {
  // Delay completion of first write.
  serial()->set_writes_paused(true);

  const std::vector<uint8_t> kUartCmd0 = {
      BtHciPacketIndicator::kHciCommand,  // UART packet indicator
      0x00,                               // arbitrary payload
  };
  const std::vector<uint8_t> kCmd0(kUartCmd0.begin() + 1, kUartCmd0.end());
  zx_status_t write_status =
      cmd_chan()->write(/*flags=*/0, kCmd0.data(), static_cast<uint32_t>(kCmd0.size()),
                        /*handles=*/nullptr,
                        /*num_handles=*/0);
  EXPECT_EQ(write_status, ZX_OK);
  // We have not run the loop, so the write should not be processed yet.
  EXPECT_EQ(serial()->writes().size(), 0u);

  const std::vector<uint8_t> kUartCmd1 = {
      BtHciPacketIndicator::kHciCommand,  // UART packet indicator
      0x01,                               // arbitrary payload
  };
  const std::vector<uint8_t> kCmd1(kUartCmd1.begin() + 1, kUartCmd1.end());
  write_status = cmd_chan()->write(/*flags=*/0, kCmd1.data(), static_cast<uint32_t>(kCmd1.size()),
                                   /*handles=*/nullptr,
                                   /*num_handles=*/0);
  EXPECT_EQ(write_status, ZX_OK);
  RunLoopUntilIdle();

  // Call the first command's completion callback. This should resume waiting for signals.
  serial()->set_writes_paused(false);
  // Wait for the readable signal to be processed.
  RunLoopUntilIdle();

  const std::vector<std::vector<uint8_t>>& packets = serial()->writes();
  ASSERT_EQ(packets.size(), 2u);
  EXPECT_EQ(packets[0], kUartCmd0);
  EXPECT_EQ(packets[1], kUartCmd1);
}

TEST_F(BtTransportUartHciProtocolTest, ReceiveManyHciEventsSplitIntoTwoResponses) {
  const std::vector<uint8_t> kSnoopEventBuffer = {
      BT_HCI_SNOOP_TYPE_EVT | BT_HCI_SNOOP_FLAG_RECV,  // Snoop packet flag
      0x01,                                            // event code
      0x02,                                            // parameter_total_size
      0x03,                                            // arbitrary parameter
      0x04                                             // arbitrary parameter
  };
  const std::vector<uint8_t> kEventBuffer(kSnoopEventBuffer.begin() + 1, kSnoopEventBuffer.end());
  std::vector<uint8_t> kSerialEventBuffer = kSnoopEventBuffer;
  kSerialEventBuffer[0] = BtHciPacketIndicator::kHciEvent;
  const std::vector<uint8_t> kPart1(kSerialEventBuffer.begin(), kSerialEventBuffer.begin() + 3);
  const std::vector<uint8_t> kPart2(kSerialEventBuffer.begin() + 3, kSerialEventBuffer.end());

  const int kNumEvents = 20;
  for (int i = 0; i < kNumEvents; i++) {
    serial()->QueueReadValue(kPart1);
    serial()->QueueReadValue(kPart2);
    RunLoopUntilIdle();
  }

  ASSERT_EQ(hci_events().size(), static_cast<size_t>(kNumEvents));
  for (const std::vector<uint8_t>& event : hci_events()) {
    EXPECT_EQ(event, kEventBuffer);
  }

  ASSERT_EQ(snoop_packets().size(), static_cast<size_t>(kNumEvents));
  for (const std::vector<uint8_t>& packet : snoop_packets()) {
    EXPECT_EQ(packet, kSnoopEventBuffer);
  }
}

}  // namespace
