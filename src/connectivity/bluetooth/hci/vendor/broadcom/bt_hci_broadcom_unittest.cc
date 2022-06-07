// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bt_hci_broadcom.h"

#include <fuchsia/hardware/bt/hci/cpp/banjo.h>
#include <fuchsia/hardware/serialimpl/async/cpp/banjo.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/ddk/metadata.h>

#include <gtest/gtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace bt_hci_broadcom {

namespace {

// Firmware binaries are a sequence of HCI commands containing the firmware as payloads. For
// testing, we use 1 HCI command with a 1 byte payload.
const std::vector<uint8_t> kFirmware = {
    0x01, 0x02,  // arbitrary "firmware opcode"
    0x01,        // parameter_total_size
    0x03         // payload
};
const char* kFirmwarePath = "BCM4345C5.hcd";

const std::array<uint8_t, 6> kMacAddress = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};

const std::array<uint8_t, 6> kCommandCompleteEvent = {
    0x0e,        // command complete event code
    0x04,        // parameter_total_size
    0x01,        // num_hci_command_packets
    0x00, 0x00,  // command opcode (hardcoded for simplicity since this isn't checked by the driver)
    0x00,        // return_code (success)
};

class FakeTransportDevice : public ddk::BtHciProtocol<FakeTransportDevice>,
                            public ddk::SerialImplAsyncProtocol<FakeTransportDevice> {
 public:
  explicit FakeTransportDevice(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}
  serial_impl_async_protocol_t serial_proto() const {
    serial_impl_async_protocol_t proto;
    proto.ctx = const_cast<FakeTransportDevice*>(this);
    proto.ops = const_cast<serial_impl_async_protocol_ops_t*>(&serial_impl_async_protocol_ops_);
    return proto;
  }

  bt_hci_protocol_t hci_proto() const {
    bt_hci_protocol_t proto;
    proto.ctx = const_cast<FakeTransportDevice*>(this);
    proto.ops = const_cast<bt_hci_protocol_ops_t*>(&bt_hci_protocol_ops_);
    return proto;
  }

  // Set a custom handler for commands. If null, command complete events will be automatically sent.
  void SetCommandHandler(fit::function<void(std::vector<uint8_t>)> command_callback) {
    command_callback_ = std::move(command_callback);
  }

  zx::channel& command_chan() { return command_channel_; }

  // ddk::BtHciProtocol mixins:
  zx_status_t BtHciOpenCommandChannel(zx::channel channel) {
    command_channel_ = std::move(channel);
    cmd_chan_wait_.set_object(command_channel_.get());
    cmd_chan_wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
    cmd_chan_wait_.Begin(dispatcher_);
    return ZX_OK;
  }
  zx_status_t BtHciOpenAclDataChannel(zx::channel channel) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t BtHciOpenScoChannel(zx::channel channel) { return ZX_ERR_NOT_SUPPORTED; }
  void BtHciConfigureSco(sco_coding_format_t coding_format, sco_encoding_t encoding,
                         sco_sample_rate_t sample_rate, bt_hci_configure_sco_callback callback,
                         void* cookie) {}
  void BtHciResetSco(bt_hci_reset_sco_callback callback, void* cookie) {}
  zx_status_t BtHciOpenSnoopChannel(zx::channel channel) { return ZX_ERR_NOT_SUPPORTED; }

  // ddk::SerialImplAsyncProtocol mixins:
  zx_status_t SerialImplAsyncGetInfo(serial_port_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t SerialImplAsyncConfig(uint32_t baud_rate, uint32_t flags) { return ZX_OK; }
  zx_status_t SerialImplAsyncEnable(bool enable) { return ZX_ERR_NOT_SUPPORTED; }
  void SerialImplAsyncReadAsync(serial_impl_async_read_async_callback callback, void* cookie) {}
  void SerialImplAsyncWriteAsync(const uint8_t* buf_buffer, size_t buf_size,
                                 serial_impl_async_write_async_callback callback, void* cookie) {}
  void SerialImplAsyncCancelAll() {}

 private:
  void OnCommandChannelSignal(async_dispatcher_t*, async::WaitBase* wait, zx_status_t status,
                              const zx_packet_signal_t* signal) {
    ASSERT_EQ(status, ZX_OK);
    if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
      command_channel_.reset();
      return;
    }
    ASSERT_TRUE(signal->observed & ZX_CHANNEL_READABLE);
    // Make buffer large enough to hold largest command packet.
    std::vector<uint8_t> bytes(
        sizeof(HciCommandHeader) +
        std::numeric_limits<decltype(HciCommandHeader::parameter_total_size)>::max());
    uint32_t actual_bytes = 0;
    zx_status_t read_status = command_channel_.read(
        /*flags=*/0, bytes.data(), /*handles=*/nullptr, static_cast<uint32_t>(bytes.size()),
        /*num_handles=*/0, &actual_bytes, /*actual_handles=*/nullptr);
    ASSERT_EQ(read_status, ZX_OK);
    bytes.resize(actual_bytes);

    cmd_chan_received_packets_.push_back(bytes);

    if (command_callback_) {
      command_callback_(std::move(bytes));
    } else {
      zx_status_t write_status = command_channel_.write(/*flags=*/0, kCommandCompleteEvent.data(),
                                                        kCommandCompleteEvent.size(),
                                                        /*handles=*/nullptr, /*num_handles=*/0);
      EXPECT_EQ(write_status, ZX_OK);
    }

    // The wait needs to be restarted.
    zx_status_t wait_begin_status = wait->Begin(dispatcher_);
    ASSERT_EQ(wait_begin_status, ZX_OK) << zx_status_get_string(wait_begin_status);
  }

  fit::function<void(std::vector<uint8_t>)> command_callback_;
  zx::channel command_channel_;
  std::vector<std::vector<uint8_t>> cmd_chan_received_packets_;
  async::WaitMethod<FakeTransportDevice, &FakeTransportDevice::OnCommandChannelSignal>
      cmd_chan_wait_{this};
  async_dispatcher_t* dispatcher_;
};

using TestBase = ::gtest::TestLoopFixture;
class BtHciBroadcomTest : public TestBase {
 public:
  BtHciBroadcomTest() : fake_transport_device_(dispatcher()) {}

  void SetUp() override {
    TestBase::SetUp();
    root_device_ = MockDevice::FakeRootParent();
    root_device_->AddProtocol(ZX_PROTOCOL_BT_HCI, fake_transport_device_.hci_proto().ops,
                              fake_transport_device_.serial_proto().ctx);
    root_device_->AddProtocol(ZX_PROTOCOL_SERIAL_IMPL_ASYNC,
                              fake_transport_device_.serial_proto().ops,
                              fake_transport_device_.serial_proto().ctx);

    ASSERT_EQ(BtHciBroadcom::Create(/*ctx=*/nullptr, root_device_.get(), dispatcher()), ZX_OK);
    ASSERT_TRUE(dut());

    // TODO(fxbug.dev/91487): Due to Mock DDK limitations, we need to add the BT_VENDOR protocol to
    // the BtTransportUart MockDevice so that BtHciProtocolClient (and device_get_protocol) work.
    bt_vendor_protocol_t vendor_proto;
    dut()->GetDeviceContext<BtHciBroadcom>()->DdkGetProtocol(ZX_PROTOCOL_BT_VENDOR, &vendor_proto);
    dut()->AddProtocol(ZX_PROTOCOL_BT_VENDOR, vendor_proto.ops, vendor_proto.ctx);
  }

  void TearDown() override {
    RunLoopUntilIdle();
    dut()->UnbindOp();
    EXPECT_EQ(dut()->UnbindReplyCallStatus(), ZX_OK);
    dut()->ReleaseOp();
    TestBase::TearDown();
  }

 protected:
  void SetFirmware() { dut()->SetFirmware(kFirmware, kFirmwarePath); }

  void SetMetadata() {
    root_dev()->SetMetadata(DEVICE_METADATA_MAC_ADDRESS, kMacAddress.data(), kMacAddress.size());
  }

  [[nodiscard]] zx_status_t InitDut() {
    dut()->InitOp();
    // Ensure delays fire.
    RunLoopRepeatedlyFor(zx::sec(1));
    return dut()->InitReplyCallStatus();
  }

  MockDevice* root_dev() const { return root_device_.get(); }

  MockDevice* dut() const { return root_device_->GetLatestChild(); }

  FakeTransportDevice* transport() { return &fake_transport_device_; }

 private:
  std::shared_ptr<MockDevice> root_device_;
  FakeTransportDevice fake_transport_device_;
};

class BtHciBroadcomInitializedTest : public BtHciBroadcomTest {
 public:
  void SetUp() override {
    BtHciBroadcomTest::SetUp();
    SetFirmware();
    SetMetadata();
    ASSERT_EQ(InitDut(), ZX_OK);
  }
};

TEST_F(BtHciBroadcomInitializedTest, Lifecycle) {}

TEST_F(BtHciBroadcomTest, ReportLoadFirmwareError) {
  // Ensure reading metadata succeeds.
  SetMetadata();

  // No firmware has been set, so load_firmware() should fail during initialization.
  ASSERT_EQ(InitDut(), ZX_ERR_NOT_FOUND);
}

TEST_F(BtHciBroadcomTest, TooSmallFirmwareBuffer) {
  // Ensure reading metadata succeeds.
  SetMetadata();

  dut()->SetFirmware(std::vector<uint8_t>{0x00});
  ASSERT_EQ(InitDut(), ZX_ERR_INTERNAL);
}

TEST_F(BtHciBroadcomInitializedTest, GetFeatures) {
  ddk::BtVendorProtocolClient client(dut());
  ASSERT_TRUE(client.is_valid());
  EXPECT_EQ(client.GetFeatures(), BT_VENDOR_FEATURES_SET_ACL_PRIORITY_COMMAND);
}

TEST_F(BtHciBroadcomTest, ControllerReturnsEventSmallerThanEventHeader) {
  transport()->SetCommandHandler([this](const std::vector<uint8_t>& command) {
    zx_status_t write_status =
        transport()->command_chan().write(/*flags=*/0, kCommandCompleteEvent.data(),
                                          /*num_bytes=*/1,
                                          /*handles=*/nullptr, /*num_handles=*/0);
    EXPECT_EQ(write_status, ZX_OK);
  });

  SetFirmware();
  SetMetadata();
  ASSERT_NE(InitDut(), ZX_OK);
}

TEST_F(BtHciBroadcomTest, ControllerReturnsEventSmallerThanCommandComplete) {
  transport()->SetCommandHandler([this](const std::vector<uint8_t>& command) {
    zx_status_t write_status =
        transport()->command_chan().write(/*flags=*/0, kCommandCompleteEvent.data(),
                                          /*num_bytes=*/sizeof(HciEventHeader),
                                          /*handles=*/nullptr, /*num_handles=*/0);
    EXPECT_EQ(write_status, ZX_OK);
  });

  SetFirmware();
  SetMetadata();
  ASSERT_NE(InitDut(), ZX_OK);
}

TEST_F(BtHciBroadcomTest, ControllerReturnsBdaddrEventWithoutBdaddrParam) {
  // Set an invalid mac address in the metadata so that a ReadBdaddr command is sent to get fallback
  // address.
  root_dev()->SetMetadata(DEVICE_METADATA_MAC_ADDRESS, kMacAddress.data(), kMacAddress.size() - 1);
  SetFirmware();
  // Respond to ReadBdaddr command with a command complete (which doesn't include the bdaddr).
  transport()->SetCommandHandler([this](auto) {
    zx_status_t write_status =
        transport()->command_chan().write(/*flags=*/0, kCommandCompleteEvent.data(),
                                          /*num_bytes=*/kCommandCompleteEvent.size(),
                                          /*handles=*/nullptr, /*num_handles=*/0);
    EXPECT_EQ(write_status, ZX_OK);
  });

  // Ensure loading the firmware succeeds.
  SetFirmware();

  // Initialization should still succeed (an error will be logged, but it's not fatal).
  ASSERT_EQ(InitDut(), ZX_OK);
}

TEST_F(BtHciBroadcomInitializedTest, EncodeSetAclPrioritySuccessWithParametersHighSink) {
  ddk::BtVendorProtocolClient client(dut());
  ASSERT_TRUE(client.is_valid());

  std::array<uint8_t, sizeof(BcmSetAclPriorityCmd)> buffer;
  size_t actual_size = 0;
  bt_vendor_params_t params = {.set_acl_priority = {
                                   .connection_handle = 0xFF00,
                                   .priority = BT_VENDOR_ACL_PRIORITY_HIGH,
                                   .direction = BT_VENDOR_ACL_DIRECTION_SINK,
                               }};
  ASSERT_EQ(ZX_OK, client.EncodeCommand(BT_VENDOR_COMMAND_SET_ACL_PRIORITY, &params, buffer.data(),
                                        buffer.size(), &actual_size));
  ASSERT_EQ(buffer.size(), actual_size);
  const std::array<uint8_t, sizeof(BcmSetAclPriorityCmd)> kExpectedBuffer = {
      0x1A,
      0xFD,  // OpCode
      0x04,  // size
      0x00,
      0xFF,                  // handle
      kBcmAclPriorityHigh,   // priority
      kBcmAclDirectionSink,  // direction
  };
  EXPECT_EQ(buffer, kExpectedBuffer);
}

TEST_F(BtHciBroadcomInitializedTest, EncodeSetAclPrioritySuccessWithParametersNormalSource) {
  ddk::BtVendorProtocolClient client(dut());
  ASSERT_TRUE(client.is_valid());

  std::array<uint8_t, sizeof(BcmSetAclPriorityCmd)> buffer;
  size_t actual_size = 0;
  bt_vendor_params_t params = {.set_acl_priority = {
                                   .connection_handle = 0xFF00,
                                   .priority = BT_VENDOR_ACL_PRIORITY_NORMAL,
                                   .direction = BT_VENDOR_ACL_DIRECTION_SOURCE,
                               }};
  ASSERT_EQ(ZX_OK, client.EncodeCommand(BT_VENDOR_COMMAND_SET_ACL_PRIORITY, &params, buffer.data(),
                                        buffer.size(), &actual_size));
  ASSERT_EQ(buffer.size(), actual_size);
  const std::array<uint8_t, sizeof(BcmSetAclPriorityCmd)> kExpectedBuffer = {
      0x1A,
      0xFD,  // OpCode
      0x04,  // size
      0x00,
      0xFF,                    // handle
      kBcmAclPriorityNormal,   // priority
      kBcmAclDirectionSource,  // direction
  };
  EXPECT_EQ(buffer, kExpectedBuffer);
}

TEST_F(BtHciBroadcomInitializedTest, EncodeSetAclPriorityBufferTooSmall) {
  ddk::BtVendorProtocolClient client(dut());
  ASSERT_TRUE(client.is_valid());

  std::array<uint8_t, sizeof(BcmSetAclPriorityCmd) - 1> buffer;
  size_t actual_size = 0;
  bt_vendor_params_t params = {.set_acl_priority = {
                                   .connection_handle = 0xFF00,
                                   .priority = BT_VENDOR_ACL_PRIORITY_HIGH,
                                   .direction = BT_VENDOR_ACL_DIRECTION_SINK,
                               }};
  ASSERT_EQ(ZX_ERR_BUFFER_TOO_SMALL,
            client.EncodeCommand(BT_VENDOR_COMMAND_SET_ACL_PRIORITY, &params, buffer.data(),
                                 buffer.size(), &actual_size));
  ASSERT_EQ(actual_size, 0u);
}

TEST_F(BtHciBroadcomInitializedTest, EncodeUnsupportedCommand) {
  ddk::BtVendorProtocolClient client(dut());
  ASSERT_TRUE(client.is_valid());

  uint8_t buffer[20];
  size_t actual_size = 0;
  bt_vendor_params_t params;
  const bt_vendor_command_t kUnsupportedCommand = 0xFF;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, client.EncodeCommand(kUnsupportedCommand, &params, buffer,
                                                      sizeof(buffer), &actual_size));
}

}  // namespace

}  // namespace bt_hci_broadcom
