// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.fido.report/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/errors.h>

#include <src/devices/testing/mock-ddk/mock-device.h>
#include <zxtest/zxtest.h>

#include "ctaphid.h"
#include "fidl/fuchsia.fido.report/cpp/markers.h"

namespace ctaphid {
/// Exact report descriptor for a Yubico 5 series security key (note the 0xF1DO near the start).
#define SKEY_DESC_LEN 34
const uint8_t skey_desc[SKEY_DESC_LEN] = {
    0x06, 0xd0, 0xf1,  // Usage Page ( FIDO_USAGE_PAGE )
    0x09, 0x01,        // Usage ( FIDO_USAGE_CTAPHID )
    0xA1, 0x01,        // Collection ( Application )
    0x09, 0x20,        //     HID_Usage ( FIDO_USAGE_DATA_IN )
    0x15, 0x00,        //     Usage Minimum ( 0x00 )
    0x26, 0xff,        //     Usage Maximum ( 0xff )
    0x00, 0x75, 0x08,  //     HID_ReportSize ( 8 ),
    0x95, 0x40,        //     HID_ReportCount ( HID_INPUT_REPORT_BYTES )
    0x81, 0x02,        //     HID_Input ( HID_Data | HID_Absolute | HID_Variable ),
    0x09, 0x21,        //     HID_Usage ( FIDO_USAGE_DATA_OUT ),
    0x15, 0x00,        //     Usage Minimum ( 0x00 )
    0x26, 0xff,        //     Usage Maximum ( 0xff )
    0x00, 0x75, 0x08,  //     HID_ReportSize ( 8 ),
    0x95, 0x40,        //     HID_ReportCount ( HID_INPUT_REPORT_BYTES )
    0x91, 0x02,        //     HID_Output ( HID_Data | HID_Absolute | HID_Variable ),
    0xc0,              // End Collection
};

class FakeCtapHidDevice : public ddk::HidDeviceProtocol<FakeCtapHidDevice> {
 public:
  FakeCtapHidDevice() : proto_({&hid_device_protocol_ops_, this}) {}

  zx_status_t HidDeviceRegisterListener(const hid_report_listener_protocol_t* listener) {
    listener_ = *listener;
    return ZX_OK;
  }

  void HidDeviceUnregisterListener() { listener_.reset(); }

  zx_status_t HidDeviceGetDescriptor(uint8_t* out_descriptor_list, size_t descriptor_count,
                                     size_t* out_descriptor_actual) {
    if (descriptor_count < report_desc_.size()) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(out_descriptor_list, report_desc_.data(), report_desc_.size());
    *out_descriptor_actual = report_desc_.size();
    return ZX_OK;
  }

  zx_status_t HidDeviceGetReport(hid_report_type_t rpt_type, uint8_t rpt_id,
                                 uint8_t* out_report_list, size_t report_count,
                                 size_t* out_report_actual) {
    // If the client is Getting a report with a specific ID, check that it matches
    // our saved report.
    if ((rpt_id != 0) && (report_.size() > 0)) {
      if (rpt_id != report_[0]) {
        return ZX_ERR_WRONG_TYPE;
      }
    }

    if (report_count < report_.size()) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(out_report_list, report_.data(), report_.size());
    *out_report_actual = report_.size();

    return ZX_OK;
  }

  void HidDeviceGetHidDeviceInfo(hid_device_info_t* out_info) {
    out_info->vendor_id = 0xabc;
    out_info->product_id = 123;
    out_info->version = 5;
  }

  zx_status_t HidDeviceSetReport(hid_report_type_t rpt_type, uint8_t rpt_id,
                                 const uint8_t* report_list, size_t report_count) {
    report_ = std::vector<uint8_t>(report_list, report_list + report_count);
    n_set_reports_received++;
    return ZX_OK;
  }

  void SetReportDesc(std::vector<uint8_t> report_desc) { report_desc_ = report_desc; }

  void SendReport(const std::vector<uint8_t>& report, zx_time_t timestamp = ZX_TIME_INFINITE) {
    if (timestamp == ZX_TIME_INFINITE) {
      timestamp = zx_clock_get_monotonic();
    }
    if (listener_.has_value()) {
      listener_->ops->receive_report(listener_->ctx, report.data(), report.size(), timestamp);
    }
  }

  void reset_set_reports_counter() { n_set_reports_received = 0; }
  void reset_packets_received_counter() { n_packets_received = 0; }

  std::optional<hid_report_listener_protocol_t> listener_;
  hid_device_protocol_t proto_;
  std::vector<uint8_t> report_desc_;

  std::vector<uint8_t> report_;
  uint32_t n_set_reports_received = 0;
  uint32_t n_packets_received = 0;
};

class CtapHidDevTest : public zxtest::Test {
 public:
  CtapHidDevTest()
      : loop_(&kAsyncLoopConfigNeverAttachToThread), mock_parent_(MockDevice::FakeRootParent()) {}
  void SetUp() override {
    hid_client_ = ddk::HidDeviceProtocolClient(&fake_hid_device_.proto_);
    ctap_driver_device_ = new CtapHidDriver(mock_parent_.get(), hid_client_);
    // Each test is responsible for calling |ctap_driver_device_->Bind()|.
  }

 protected:
  static constexpr size_t kFidlReportBufferSize = 8192;

  void SetupSyncClient() {
    ASSERT_OK(loop_.StartThread("test-loop-thread"));
    auto endpoints = fidl::CreateEndpoints<fuchsia_fido_report::SecurityKeyDevice>();
    ASSERT_OK(endpoints.status_value());
    binding_ = fidl::BindServer<fidl::WireServer<fuchsia_fido_report::SecurityKeyDevice>>(
        loop_.dispatcher(), std::move(endpoints->server), ctap_driver_device_);
    sync_client_.Bind(std::move(endpoints->client));
  }

  void SetupAsyncClient() {
    auto endpoints = fidl::CreateEndpoints<fuchsia_fido_report::SecurityKeyDevice>();
    async_client_.Bind(std::move(endpoints->client), loop_.dispatcher());
    fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), ctap_driver_device_);
  }

  fuchsia_fido_report::wire::Message BuildRequest(fidl::Arena<kFidlReportBufferSize>& allocator,
                                                  uint32_t channel,
                                                  fuchsia_fido_report::CtapHidCommand command,
                                                  std::vector<uint8_t>& data) {
    auto fidl_skey_request_builder_ = fuchsia_fido_report::wire::Message::Builder(allocator);
    fidl_skey_request_builder_.channel_id(channel);
    fidl_skey_request_builder_.command_id(command);
    fidl_skey_request_builder_.data(fidl::VectorView<uint8_t>::FromExternal(data));
    fidl_skey_request_builder_.payload_len(static_cast<uint16_t>(data.size()));
    auto result = fidl_skey_request_builder_.Build();
    return result;
  }

  async::Loop loop_;

  std::shared_ptr<MockDevice> mock_parent_;
  FakeCtapHidDevice fake_hid_device_;
  ddk::HidDeviceProtocolClient hid_client_;
  CtapHidDriver* ctap_driver_device_;

  std::optional<fidl::ServerBindingRef<fuchsia_fido_report::SecurityKeyDevice>> binding_;
  fidl::WireSyncClient<fuchsia_fido_report::SecurityKeyDevice> sync_client_;
  fidl::WireClient<fuchsia_fido_report::SecurityKeyDevice> async_client_;
};

TEST_F(CtapHidDevTest, HidLifetimeTest) {
  std::vector<uint8_t> desc(skey_desc, skey_desc + sizeof(skey_desc));
  fake_hid_device_.SetReportDesc(desc);

  ASSERT_OK(ctap_driver_device_->Bind());
  ASSERT_TRUE(fake_hid_device_.listener_);

  // make sure the child device is there
  ASSERT_EQ(mock_parent_->child_count(), 1);
  auto* child = mock_parent_->GetLatestChild();

  child->ReleaseOp();

  // Make sure that the CtapHidDriver class has unregistered from the HID device.
  ASSERT_FALSE(fake_hid_device_.listener_);
}

TEST_F(CtapHidDevTest, SendMessageWithEmptyPayloadTest) {
  std::vector<uint8_t> desc(skey_desc, skey_desc + SKEY_DESC_LEN);
  fake_hid_device_.SetReportDesc(desc);
  ASSERT_OK(ctap_driver_device_->Bind());

  SetupSyncClient();

  fidl::Arena<kFidlReportBufferSize> allocator;
  std::vector<uint8_t> data_vec{};
  auto message_request =
      BuildRequest(allocator, 0xFFFFFFFF, fuchsia_fido_report::CtapHidCommand::kInit, data_vec);

  // Send the Command.
  fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage> result =
      sync_client_->SendMessage(message_request);
  loop_.RunUntilIdle();

  ASSERT_EQ(result.status(), ZX_OK);
  // Check the hid driver received the correct number of packets.
  ASSERT_EQ(fake_hid_device_.n_set_reports_received, 1);
}

TEST_F(CtapHidDevTest, SendMessageSinglePacketTest) {
  std::vector<uint8_t> desc(skey_desc, skey_desc + SKEY_DESC_LEN);
  fake_hid_device_.SetReportDesc(desc);
  ASSERT_OK(ctap_driver_device_->Bind());

  SetupSyncClient();

  fidl::Arena<kFidlReportBufferSize> allocator;
  std::vector<uint8_t> data_vec{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  auto message_request =
      BuildRequest(allocator, 0xFFFFFFFF, fuchsia_fido_report::CtapHidCommand::kInit, data_vec);

  // Send the Command.
  fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage> result =
      sync_client_->SendMessage(message_request);
  loop_.RunUntilIdle();

  ASSERT_EQ(result.status(), ZX_OK);
  // Check the hid driver received the correct number of packets.
  ASSERT_EQ(fake_hid_device_.n_set_reports_received, 1);
}

TEST_F(CtapHidDevTest, SendMessageMultiPacketTest) {
  std::vector<uint8_t> desc(skey_desc, skey_desc + SKEY_DESC_LEN);
  fake_hid_device_.SetReportDesc(desc);
  ASSERT_OK(ctap_driver_device_->Bind());

  SetupSyncClient();

  fidl::Arena<kFidlReportBufferSize> allocator;
  std::vector<uint8_t> data_vec(1024, 1);
  auto message_request =
      BuildRequest(allocator, 0xFFFFFFFF, fuchsia_fido_report::CtapHidCommand::kInit, data_vec);

  // Send the Command.
  fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage> result =
      sync_client_->SendMessage(message_request);
  loop_.RunUntilIdle();

  ASSERT_EQ(result.status(), ZX_OK);
  // The Driver should have split this command into multiple packets.
  // The number of packets used to send this message should be:
  // ciel((data_size - (ouput_packet_size-7)) / (output_packet_size - 5)) + 1
  // In this case, the output_packet_size is 64 and the data_size is 1024.
  ASSERT_EQ(fake_hid_device_.n_set_reports_received, 18);
}

TEST_F(CtapHidDevTest, SendMessageChannelAlreadyPendingTest) {
  std::vector<uint8_t> desc(skey_desc, skey_desc + SKEY_DESC_LEN);
  fake_hid_device_.SetReportDesc(desc);
  ASSERT_OK(ctap_driver_device_->Bind());

  SetupSyncClient();

  uint32_t test_channel = 0x01020304;

  // Send a Command on test_channel.
  {
    fidl::Arena<kFidlReportBufferSize> allocator;
    std::vector<uint8_t> data_vec{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    auto message_request =
        BuildRequest(allocator, test_channel, fuchsia_fido_report::CtapHidCommand::kInit, data_vec);

    // Send the Command.
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage> result =
        sync_client_->SendMessage(message_request);
    loop_.RunUntilIdle();

    ASSERT_EQ(result.status(), ZX_OK);
  }

  // Send another Command on the same channel. This should fail since we are pending on a response
  // from the key for the original request.
  {
    fidl::Arena<kFidlReportBufferSize> allocator;
    std::vector<uint8_t> data_vec{0xde, 0xad, 0xbe, 0xef};
    auto message_request =
        BuildRequest(allocator, test_channel, fuchsia_fido_report::CtapHidCommand::kMsg, data_vec);

    // Send the Command.
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage> result =
        sync_client_->SendMessage(message_request);
    loop_.RunUntilIdle();

    ASSERT_TRUE(result->is_error());
    ASSERT_EQ(result->error_value(), ZX_ERR_UNAVAILABLE);
  }

  // Have the key reply to the first command.
  {
    std::vector<uint8_t> packet{
        // channel id
        static_cast<uint8_t>((test_channel >> 24) & 0xff),
        static_cast<uint8_t>((test_channel >> 16) & 0xff),
        static_cast<uint8_t>((test_channel >> 8) & 0xff), static_cast<uint8_t>(test_channel & 0xff),
        // command id with init packet bit set
        static_cast<uint8_t>(fuchsia_fido_report::CtapHidCommand::kInit) | (1u << 7),
        // payload len
        0x00, 0x01,
        // payload
        0x0f};
    fake_hid_device_.SendReport(packet);
    loop_.RunUntilIdle();
  }

  // Send another Command on the same channel again. This should still fail since we still need to
  // get the response from the original request.
  {
    fidl::Arena<kFidlReportBufferSize> allocator;
    std::vector<uint8_t> data_vec{0xde, 0xad, 0xbe, 0xef};
    auto message_request =
        BuildRequest(allocator, test_channel, fuchsia_fido_report::CtapHidCommand::kMsg, data_vec);

    // Send the Command.
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage> result =
        sync_client_->SendMessage(message_request);
    loop_.RunUntilIdle();

    ASSERT_TRUE(result->is_error());
    ASSERT_EQ(result->error_value(), ZX_ERR_UNAVAILABLE);
  }

  // Get the response to the original command.
  {
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::GetMessage> result =
        sync_client_->GetMessage(test_channel);
    loop_.RunUntilIdle();
  }

  // Retry sending another Command on the same channel. This should now succeed since the first
  // transaction has completed.
  {
    fidl::Arena<kFidlReportBufferSize> allocator;
    std::vector<uint8_t> data_vec{0xde, 0xad, 0xbe, 0xef};
    auto message_request =
        BuildRequest(allocator, test_channel, fuchsia_fido_report::CtapHidCommand::kMsg, data_vec);

    // Send the Command.
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage> result =
        sync_client_->SendMessage(message_request);
    loop_.RunUntilIdle();

    ASSERT_FALSE(result->is_error());
    ;
  }
}

TEST_F(CtapHidDevTest, SendMessageDeviceBusyTest) {
  std::vector<uint8_t> desc(skey_desc, skey_desc + SKEY_DESC_LEN);
  fake_hid_device_.SetReportDesc(desc);
  ASSERT_OK(ctap_driver_device_->Bind());

  SetupSyncClient();

  uint32_t test_channel = 0x01020304;
  uint8_t test_payload_byte = 0x0f;
  uint32_t other_test_channel = 0x09080706;

  // Send a Command on test_channel.
  {
    fidl::Arena<kFidlReportBufferSize> allocator;
    std::vector<uint8_t> data_vec{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    auto message_request =
        BuildRequest(allocator, test_channel, fuchsia_fido_report::CtapHidCommand::kMsg, data_vec);

    // Send the Command.
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage> result =
        sync_client_->SendMessage(message_request);
    loop_.RunUntilIdle();

    ASSERT_EQ(result.status(), ZX_OK);
  }

  // Send another Command on a different channel.
  // This should fail as we're still waiting for a response on the first request.
  {
    fidl::Arena<kFidlReportBufferSize> allocator;
    std::vector<uint8_t> data_vec{0x01};
    auto message_request = BuildRequest(allocator, other_test_channel,
                                        fuchsia_fido_report::CtapHidCommand::kMsg, data_vec);

    // Send the Command.
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage> result =
        sync_client_->SendMessage(message_request);
    loop_.RunUntilIdle();

    ASSERT_TRUE(result->is_error());
    ASSERT_EQ(result->error_value(), ZX_ERR_UNAVAILABLE);
  }

  // Have the key reply to the first command.
  {
    std::vector<uint8_t> packet{
        // channel id
        static_cast<uint8_t>((test_channel >> 24) & 0xff),
        static_cast<uint8_t>((test_channel >> 16) & 0xff),
        static_cast<uint8_t>((test_channel >> 8) & 0xff), static_cast<uint8_t>(test_channel & 0xff),
        // command id with init packet bit set
        static_cast<uint8_t>(fuchsia_fido_report::CtapHidCommand::kInit) | (1u << 7),
        // payload len
        0x00, 0x01,
        // payload
        test_payload_byte};
    fake_hid_device_.SendReport(packet);
    loop_.RunUntilIdle();
  }

  // Try again to send another Command on a different channel.
  // This should still fail as the first request's response still needs to be retrieved.
  {
    fidl::Arena<kFidlReportBufferSize> allocator;
    std::vector<uint8_t> data_vec{0x01};
    auto message_request = BuildRequest(allocator, other_test_channel,
                                        fuchsia_fido_report::CtapHidCommand::kMsg, data_vec);

    // Send the Command.
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage> result =
        sync_client_->SendMessage(message_request);
    loop_.RunUntilIdle();

    ASSERT_TRUE(result->is_error());
    ASSERT_EQ(result->error_value(), ZX_ERR_UNAVAILABLE);
  }

  // Get the response to the first command, on test_channel.
  {
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::GetMessage> result =
        sync_client_->GetMessage(test_channel);
    loop_.RunUntilIdle();
  }

  // Finally try to send another Command on a different channel.
  // This should now succeed as the first transaction is complete.
  {
    fidl::Arena<kFidlReportBufferSize> allocator;
    std::vector<uint8_t> data_vec{0x01};
    auto message_request = BuildRequest(allocator, other_test_channel,
                                        fuchsia_fido_report::CtapHidCommand::kMsg, data_vec);

    // Send the Command.
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage> result =
        sync_client_->SendMessage(message_request);
    loop_.RunUntilIdle();

    ASSERT_FALSE(result->is_error());
    ASSERT_OK(result);
  }
}

TEST_F(CtapHidDevTest, ReceiveSinglePacketMessageTest) {
  std::vector<uint8_t> desc(skey_desc, skey_desc + SKEY_DESC_LEN);
  fake_hid_device_.SetReportDesc(desc);
  ASSERT_OK(ctap_driver_device_->Bind());

  SetupSyncClient();

  uint32_t test_channel = 0x01020304;
  auto test_command = fuchsia_fido_report::CtapHidCommand::kInit;
  std::vector<uint8_t> test_payload{0xde, 0xad, 0xbe, 0xef};

  // Send a SendMessage so we are able to call GetMessage.
  {
    fidl::Arena<kFidlReportBufferSize> allocator;
    std::vector<uint8_t> data_vec{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    auto message_request = BuildRequest(allocator, test_channel, test_command, data_vec);

    // Send the Command.
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage> result =
        sync_client_->SendMessage(message_request);
    loop_.RunUntilIdle();
  }

  // Send a packet from the key.
  {
    std::vector<uint8_t> packet{// channel id
                                static_cast<uint8_t>((test_channel >> 24) & 0xff),
                                static_cast<uint8_t>((test_channel >> 16) & 0xff),
                                static_cast<uint8_t>((test_channel >> 8) & 0xff),
                                static_cast<uint8_t>(test_channel & 0xff),
                                // command id with init packet bit set
                                static_cast<uint8_t>(fidl::ToUnderlying(test_command) | (1u << 7)),
                                // payload len
                                0x00, 0x04,
                                // payload
                                0xde, 0xad, 0xbe, 0xef};
    fake_hid_device_.SendReport(packet);
    loop_.RunUntilIdle();
  }

  // Get and check the Message formed from the packet.
  {
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::GetMessage> result =
        sync_client_->GetMessage(test_channel);
    loop_.RunUntilIdle();

    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_TRUE(result->value()->has_channel_id());
    ASSERT_EQ(result->value()->channel_id(), test_channel);
    ASSERT_EQ(result->value()->command_id(), test_command);
    ASSERT_EQ(result->value()->payload_len(), test_payload.size());
    for (size_t i = 0; i < test_payload.size(); i++) {
      ASSERT_EQ(result->value()->data().at(i), test_payload.at(i));
    }
  }
}

TEST_F(CtapHidDevTest, ReceiveMultiplePacketMessageTest) {
  std::vector<uint8_t> desc(skey_desc, skey_desc + SKEY_DESC_LEN);
  fake_hid_device_.SetReportDesc(desc);
  ASSERT_OK(ctap_driver_device_->Bind());

  SetupSyncClient();

  uint32_t test_channel = 0x01020304;
  auto test_command = fuchsia_fido_report::CtapHidCommand::kInit;

  uint8_t init_payload_len = 64 - 7;
  uint8_t cont_payload1_len = 64 - 5;
  uint8_t cont_payload2_len = 32;
  std::vector<uint8_t> test_init_payload(init_payload_len, 0x0a);
  std::vector<uint8_t> test_cont_payload1(cont_payload1_len, 0x0b);
  std::vector<uint8_t> test_cont_payload2(cont_payload2_len, 0x0c);

  auto total_payload(test_init_payload);
  total_payload.insert(total_payload.end(), test_cont_payload1.begin(), test_cont_payload1.end());
  total_payload.insert(total_payload.end(), test_cont_payload2.begin(), test_cont_payload2.end());
  uint16_t total_payload_len = static_cast<uint16_t>(total_payload.size());

  // Send a SendMessage so we are able to call GetMessage.
  {
    fidl::Arena<kFidlReportBufferSize> allocator;
    std::vector<uint8_t> data_vec{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    auto message_request = BuildRequest(allocator, test_channel, test_command, data_vec);

    // Send the Command.
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage> result =
        sync_client_->SendMessage(message_request);
    loop_.RunUntilIdle();
  }

  // Send the packets from the key.
  {
    // Init Payload
    std::vector<uint8_t> init_packet{
        // channel id
        static_cast<uint8_t>((test_channel >> 24) & 0xff),
        static_cast<uint8_t>((test_channel >> 16) & 0xff),
        static_cast<uint8_t>((test_channel >> 8) & 0xff), static_cast<uint8_t>(test_channel & 0xff),
        // command id with init packet bit set
        static_cast<uint8_t>(fidl::ToUnderlying(test_command) | (1u << 7)),
        // payload len
        static_cast<uint8_t>((total_payload_len >> 8) & 0xff),
        static_cast<uint8_t>(total_payload_len & 0xff)};
    init_packet.insert(init_packet.end(), test_init_payload.begin(), test_init_payload.end());
    fake_hid_device_.SendReport(init_packet);
    loop_.RunUntilIdle();
    // Cont Payload 1
    std::vector<uint8_t> cont_packet1{// channel id
                                      static_cast<uint8_t>((test_channel >> 24) & 0xff),
                                      static_cast<uint8_t>((test_channel >> 16) & 0xff),
                                      static_cast<uint8_t>((test_channel >> 8) & 0xff),
                                      static_cast<uint8_t>(test_channel & 0xff),
                                      // packet sequence number
                                      0x00};
    cont_packet1.insert(cont_packet1.end(), test_cont_payload1.begin(), test_cont_payload1.end());
    fake_hid_device_.SendReport(cont_packet1);
    loop_.RunUntilIdle();
    // Cont Payload 2
    std::vector<uint8_t> cont_packet2{// channel id
                                      static_cast<uint8_t>((test_channel >> 24) & 0xff),
                                      static_cast<uint8_t>((test_channel >> 16) & 0xff),
                                      static_cast<uint8_t>((test_channel >> 8) & 0xff),
                                      static_cast<uint8_t>(test_channel & 0xff),
                                      // packet sequence number
                                      0x01};
    cont_packet2.insert(cont_packet2.end(), test_cont_payload2.begin(), test_cont_payload2.end());
    fake_hid_device_.SendReport(cont_packet2);
    loop_.RunUntilIdle();
  }

  // Get and check the Message formed from the packet.
  {
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::GetMessage> result =
        sync_client_->GetMessage(test_channel);
    loop_.RunUntilIdle();

    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_EQ(result->value()->channel_id(), test_channel);
    ASSERT_EQ(result->value()->command_id(), test_command);
    for (size_t i = 0; i < total_payload.size(); i++) {
      ASSERT_EQ(result->value()->data().at(i), total_payload.at(i));
    }
  }
}

TEST_F(CtapHidDevTest, ReceivePacketMissingInitTest) {
  std::vector<uint8_t> desc(skey_desc, skey_desc + SKEY_DESC_LEN);
  fake_hid_device_.SetReportDesc(desc);
  ASSERT_OK(ctap_driver_device_->Bind());

  SetupSyncClient();

  uint32_t test_channel = 0x01020304;
  auto test_command = fuchsia_fido_report::CtapHidCommand::kInit;
  std::vector<uint8_t> test_payload{0xde, 0xad, 0xbe, 0xef};

  // Send a SendMessage so we are able to call GetMessage.
  {
    fidl::Arena<kFidlReportBufferSize> allocator;
    std::vector<uint8_t> data_vec{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    auto message_request = BuildRequest(allocator, test_channel, test_command, data_vec);

    // Send the Command.
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage> result =
        sync_client_->SendMessage(message_request);
    loop_.RunUntilIdle();
  }

  // Send a packet from the key.
  {
    std::vector<uint8_t> packet{// channel id
                                static_cast<uint8_t>((test_channel >> 24) & 0xff),
                                static_cast<uint8_t>((test_channel >> 16) & 0xff),
                                static_cast<uint8_t>((test_channel >> 8) & 0xff),
                                static_cast<uint8_t>(test_channel & 0xff),
                                // packet sequence number
                                0x00,
                                // payload
                                0xde, 0xad, 0xbe, 0xef};
    fake_hid_device_.SendReport(packet);
    loop_.RunUntilIdle();
  }

  // Check the response was set to an incorrect packet sequence error.
  {
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::GetMessage> result =
        sync_client_->GetMessage(test_channel);
    loop_.RunUntilIdle();

    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_EQ(result->value()->channel_id(), test_channel);
    ASSERT_EQ(result->value()->command_id(), fuchsia_fido_report::CtapHidCommand::kError);
    ASSERT_NE(result->value()->payload_len(), test_payload.size());
    ASSERT_EQ(result->value()->payload_len(), 1);
    ASSERT_NE(result->value()->data().at(0), 0x04);
  }
}

TEST_F(CtapHidDevTest, ReceivePacketMissingContTest) {
  std::vector<uint8_t> desc(skey_desc, skey_desc + SKEY_DESC_LEN);
  fake_hid_device_.SetReportDesc(desc);
  ASSERT_OK(ctap_driver_device_->Bind());

  SetupSyncClient();

  uint32_t test_channel = 0x01020304;
  auto test_command = fuchsia_fido_report::CtapHidCommand::kInit;

  uint8_t init_payload_len = 64 - 7;
  uint8_t cont_payload_len = 32;
  uint8_t total_payload_len = init_payload_len + cont_payload_len + (64 - 5);
  std::vector<uint8_t> test_init_payload(init_payload_len, 0x0a);
  std::vector<uint8_t> test_cont_payload(cont_payload_len, 0x0b);

  // Send a SendMessage so we are able to call GetMessage.
  {
    fidl::Arena<kFidlReportBufferSize> allocator;
    std::vector<uint8_t> data_vec{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    auto message_request = BuildRequest(allocator, test_channel, test_command, data_vec);

    // Send the Command.
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage> result =
        sync_client_->SendMessage(message_request);
    loop_.RunUntilIdle();
  }

  // Send an init packet from the key.
  {
    // Init Payload
    std::vector<uint8_t> init_packet{
        // channel id
        static_cast<uint8_t>((test_channel >> 24) & 0xff),
        static_cast<uint8_t>((test_channel >> 16) & 0xff),
        static_cast<uint8_t>((test_channel >> 8) & 0xff), static_cast<uint8_t>(test_channel & 0xff),
        // command id with init packet bit set
        static_cast<uint8_t>(fidl::ToUnderlying(test_command) | (1u << 7)),
        // payload len
        static_cast<uint8_t>((total_payload_len >> 8) & 0xff),
        static_cast<uint8_t>(total_payload_len & 0xff)};
    init_packet.insert(init_packet.end(), test_init_payload.begin(), test_init_payload.end());
    fake_hid_device_.SendReport(init_packet);
    loop_.RunUntilIdle();
  }

  // Send a continuation packet from the key, skipping the first packet.
  {
    std::vector<uint8_t> cont_packet{// channel id
                                     static_cast<uint8_t>((test_channel >> 24) & 0xff),
                                     static_cast<uint8_t>((test_channel >> 16) & 0xff),
                                     static_cast<uint8_t>((test_channel >> 8) & 0xff),
                                     static_cast<uint8_t>(test_channel & 0xff),
                                     // packet sequence number
                                     0x00 + 1};
    cont_packet.insert(cont_packet.end(), test_cont_payload.begin(), test_cont_payload.end());
    fake_hid_device_.SendReport(cont_packet);
    loop_.RunUntilIdle();
  }

  // Check the response was set to an incorrect packet sequence error.
  {
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::GetMessage> result =
        sync_client_->GetMessage(test_channel);
    loop_.RunUntilIdle();

    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_EQ(result->value()->channel_id(), test_channel);
    ASSERT_EQ(result->value()->command_id(), fuchsia_fido_report::CtapHidCommand::kError);
    ASSERT_EQ(result->value()->payload_len(), 1);
    ASSERT_NE(result->value()->data().at(0), 0x04);
  }
}

TEST_F(CtapHidDevTest, GetMessageChannelTest) {
  std::vector<uint8_t> desc(skey_desc, skey_desc + SKEY_DESC_LEN);
  fake_hid_device_.SetReportDesc(desc);
  ASSERT_OK(ctap_driver_device_->Bind());

  SetupSyncClient();

  uint32_t const test_channel = 0x01020304;
  auto const test_command = fuchsia_fido_report::CtapHidCommand::kMsg;
  uint8_t const test_payload_byte = 0x0f;

  // Send a SendMessage request.
  {
    fidl::Arena<kFidlReportBufferSize> allocator;
    std::vector<uint8_t> data_vec(1024, 1);
    auto message_request = BuildRequest(allocator, test_channel, test_command, data_vec);

    // Send the Command.
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage> result =
        sync_client_->SendMessage(message_request);
    loop_.RunUntilIdle();
  }

  // Set up a packet to be sent as a response.
  {
    std::vector<uint8_t> packet{// channel id
                                static_cast<uint8_t>((test_channel >> 24) & 0xff),
                                static_cast<uint8_t>((test_channel >> 16) & 0xff),
                                static_cast<uint8_t>((test_channel >> 8) & 0xff),
                                static_cast<uint8_t>(test_channel & 0xff),
                                // command id with init packet bit set
                                static_cast<uint8_t>(fidl::ToUnderlying(test_command) | (1u << 7)),
                                // payload len
                                0x00, 0x01,
                                // payload
                                test_payload_byte};
    fake_hid_device_.SendReport(packet);
    loop_.RunUntilIdle();
  }

  // Make a Request to get a message with a different channel id.
  {
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::GetMessage> result_1 =
        sync_client_->GetMessage(0xffffffff);
    loop_.RunUntilIdle();

    ASSERT_TRUE(result_1->is_error());
    ASSERT_EQ(result_1->error_value(), ZX_ERR_NOT_FOUND);
  }

  // Make a Request to get a message with the correct channel id.
  {
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::GetMessage> result_2 =
        sync_client_->GetMessage(test_channel);
    loop_.RunUntilIdle();

    ASSERT_FALSE(result_2->is_error());
    ASSERT_TRUE(result_2->value()->has_channel_id());
    ASSERT_TRUE(result_2->value()->has_data());
    ASSERT_EQ(result_2->value()->channel_id(), test_channel);

    ASSERT_EQ(result_2->value()->payload_len(), 1);
    ASSERT_EQ(result_2->value()->data().at(0), test_payload_byte);
  }
}

TEST_F(CtapHidDevTest, GetMessageKeepAliveTest) {
  std::vector<uint8_t> desc(skey_desc, skey_desc + SKEY_DESC_LEN);
  fake_hid_device_.SetReportDesc(desc);
  ASSERT_OK(ctap_driver_device_->Bind());

  SetupSyncClient();

  uint32_t test_channel = 0x01020304;
  auto test_command = fuchsia_fido_report::CtapHidCommand::kInit;
  uint8_t test_payload_byte = 0x0f;

  // Send a command.
  {
    fidl::Arena<kFidlReportBufferSize> allocator;
    std::vector<uint8_t> data_vec{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    auto message_request = BuildRequest(allocator, test_channel, test_command, data_vec);

    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage> result =
        sync_client_->SendMessage(message_request);
    loop_.RunUntilIdle();
    ASSERT_EQ(result.status(), ZX_OK);
  }

  // Set up a KEEPALIVE packet to be sent from the device.
  {
    std::vector<uint8_t> packet{
        // channel id
        static_cast<uint8_t>((test_channel >> 24) & 0xff),
        static_cast<uint8_t>((test_channel >> 16) & 0xff),
        static_cast<uint8_t>((test_channel >> 8) & 0xff), static_cast<uint8_t>(test_channel & 0xff),
        // command id with init packet bit set
        static_cast<uint8_t>(fidl::ToUnderlying(fuchsia_fido_report::CtapHidCommand::kKeepalive)) |
            (1u << 7),
        // payload len
        0x00, 0x01,
        // payload
        test_payload_byte};
    fake_hid_device_.SendReport(packet);
    loop_.RunUntilIdle();
  }

  // Make a Request to get a message. This should return the KEEPALIVE message.
  {
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::GetMessage> result =
        sync_client_->GetMessage(test_channel);
    loop_.RunUntilIdle();

    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_EQ(result->value()->command_id(), fuchsia_fido_report::CtapHidCommand::kKeepalive);
  }

  // Set up the real packet matching the original command to be sent from the device.
  {
    std::vector<uint8_t> packet{// channel id
                                static_cast<uint8_t>((test_channel >> 24) & 0xff),
                                static_cast<uint8_t>((test_channel >> 16) & 0xff),
                                static_cast<uint8_t>((test_channel >> 8) & 0xff),
                                static_cast<uint8_t>(test_channel & 0xff),
                                // command id with init packet bit set
                                static_cast<uint8_t>(fidl::ToUnderlying(test_command) | (1u << 7)),
                                // payload len
                                0x00, 0x01,
                                // payload
                                test_payload_byte};
    fake_hid_device_.SendReport(packet);
    loop_.RunUntilIdle();
  }

  // Make a Request to get a message again. This should return the final message.
  {
    fidl::WireResult<fuchsia_fido_report::SecurityKeyDevice::GetMessage> result =
        sync_client_->GetMessage(test_channel);
    loop_.RunUntilIdle();

    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_EQ(result->value()->command_id(), test_command);
  }
}

TEST_F(CtapHidDevTest, HangingGetMessageTest) {
  std::vector<uint8_t> desc(skey_desc, skey_desc + SKEY_DESC_LEN);
  fake_hid_device_.SetReportDesc(desc);
  ASSERT_OK(ctap_driver_device_->Bind());

  // Set up an async client to test GetMessage. We'll need to make the fake_hid_device send a packet
  // up to the ctaphid driver after we've sent the GetMessage() request.
  SetupAsyncClient();

  uint32_t test_channel = 0x01020304;
  auto test_command = fuchsia_fido_report::CtapHidCommand::kInit;
  uint8_t test_payload_byte = 0x0f;

  // Send a command.
  {
    fidl::Arena<kFidlReportBufferSize> allocator;
    std::vector<uint8_t> data_vec{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    auto message_request = BuildRequest(allocator, test_channel, test_command, data_vec);

    async_client_->SendMessage(message_request)
        .ThenExactlyOnce(
            [&](fidl::WireUnownedResult<fuchsia_fido_report::SecurityKeyDevice::SendMessage>&
                    result) {
              ASSERT_OK(result);
              ASSERT_FALSE(result->is_error());
            });
    loop_.RunUntilIdle();
  }

  // Make a Request to get a message. This should hang until a response is sent from the device.
  async_client_->GetMessage(test_channel)
      .ThenExactlyOnce(
          [&](fidl::WireUnownedResult<fuchsia_fido_report::SecurityKeyDevice::GetMessage>& result) {
            ASSERT_OK(result.status());
            ASSERT_FALSE(result->is_error());

            ASSERT_TRUE(result->value()->channel_id());
            ASSERT_EQ(result->value()->channel_id(), test_channel);
            ASSERT_TRUE(fidl::ToUnderlying(result->value()->command_id()));
            ASSERT_EQ(result->value()->command_id(), test_command);
            ASSERT_TRUE(result->value()->payload_len());
            ASSERT_EQ(result->value()->payload_len(), 1);
            ASSERT_TRUE(result->value()->has_data());
            ASSERT_EQ(result->value()->data().at(0), test_payload_byte);

            loop_.Quit();
          });

  // Send a response from the device.
  {
    std::vector<uint8_t> packet{// channel id
                                static_cast<uint8_t>((test_channel >> 24) & 0xff),
                                static_cast<uint8_t>((test_channel >> 16) & 0xff),
                                static_cast<uint8_t>((test_channel >> 8) & 0xff),
                                static_cast<uint8_t>(test_channel & 0xff),
                                // command id with init packet bit set
                                static_cast<uint8_t>(fidl::ToUnderlying(test_command) | (1u << 7)),
                                // payload len
                                0x00, 0x01,
                                // payload
                                test_payload_byte};
    fake_hid_device_.SendReport(packet);
    loop_.RunUntilIdle();
  }
}

}  // namespace ctaphid
