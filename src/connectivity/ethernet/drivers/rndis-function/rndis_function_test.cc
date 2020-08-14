// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rndis_function.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/sync/completion.h>

#include <ddktl/protocol/usb/function.h>
#include <zxtest/zxtest.h>

class FakeFunction : public ddk::UsbFunctionProtocol<FakeFunction, ddk::base_protocol> {
 public:
  FakeFunction() : protocol_({.ops = &usb_function_protocol_ops_, .ctx = this}) {}

  zx_status_t UsbFunctionSetInterface(const usb_function_interface_protocol_t* interface) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t UsbFunctionAllocInterface(uint8_t* out_intf_num) { return ZX_OK; }

  zx_status_t UsbFunctionAllocEp(uint8_t direction, uint8_t* out_address) {
    if (direction == USB_DIR_OUT) {
      *out_address = kBulkOutEndpoint;
    } else if (direction == USB_DIR_IN && !bulk_in_endpoint_allocated_) {
      // This unfortunately relies on the order of endpoint allocation in the driver.
      *out_address = kBulkInEndpoint;
      bulk_in_endpoint_allocated_ = true;
    } else if (direction == USB_DIR_IN && bulk_in_endpoint_allocated_) {
      *out_address = kNotificationEndpoint;
    } else {
      return ZX_ERR_BAD_STATE;
    }
    return ZX_OK;
  }

  zx_status_t UsbFunctionConfigEp(const usb_endpoint_descriptor_t* ep_desc,
                                  const usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    config_ep_calls_ += 1;
    return ZX_OK;
  }

  zx_status_t UsbFunctionDisableEp(uint8_t address) {
    disable_ep_calls_ += 1;
    return ZX_OK;
  }

  zx_status_t UsbFunctionAllocStringDesc(const char* str, uint8_t* out_index) { return ZX_OK; }

  void UsbFunctionRequestQueue(usb_request_t* usb_request,
                               const usb_request_complete_t* complete_cb) {
    usb::BorrowedRequest request(usb_request, *complete_cb, sizeof(usb_request_t));
    switch (request.request()->header.ep_address) {
      case kBulkOutEndpoint:
        pending_out_requests_.push(std::move(request));
        break;
      case kBulkInEndpoint:
        pending_in_requests_.push(std::move(request));
        break;
      case kNotificationEndpoint:
        pending_notification_requests_.push(std::move(request));
        break;
      default:
        request.Complete(ZX_ERR_INVALID_ARGS, 0);
        break;
    }
  }

  zx_status_t UsbFunctionEpSetStall(uint8_t ep_address) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t UsbFunctionEpClearStall(uint8_t ep_address) { return ZX_ERR_NOT_SUPPORTED; }

  size_t UsbFunctionGetRequestSize() {
    return usb::BorrowedRequest<>::RequestSize(sizeof(usb_request_t));
  }

  zx_status_t SetConfigured(bool configured, usb_speed_t speed) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t SetInterface(uint8_t interface, uint8_t alt_setting) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t Control(const usb_setup_t* setup, const void* write_buffer, size_t write_size,
                      void* read_buffer, size_t read_size, size_t* out_read_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t UsbFunctionCancelAll(uint8_t ep_address) {
    switch (ep_address) {
      case kBulkOutEndpoint:
        if (!pending_out_requests_.is_empty()) {
          pending_out_requests_.CompleteAll(ZX_ERR_IO_NOT_PRESENT, 0);
        }
        break;
      case kBulkInEndpoint:
        if (!pending_in_requests_.is_empty()) {
          pending_in_requests_.CompleteAll(ZX_ERR_IO_NOT_PRESENT, 0);
        }
        break;
      case kNotificationEndpoint:
        if (!pending_notification_requests_.is_empty()) {
          pending_notification_requests_.CompleteAll(ZX_ERR_IO_NOT_PRESENT, 0);
        }
        break;
      default:
        return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
  }

  const usb_function_protocol_t* Protocol() const { return &protocol_; }

  size_t ConfigEpCalls() const { return config_ep_calls_; }
  size_t DisableEpCalls() const { return disable_ep_calls_; }

  usb::BorrowedRequestQueue<void> pending_out_requests_;
  usb::BorrowedRequestQueue<void> pending_in_requests_;
  usb::BorrowedRequestQueue<void> pending_notification_requests_;

 private:
  static constexpr uint8_t kBulkOutEndpoint = 0;
  static constexpr uint8_t kBulkInEndpoint = 1;
  static constexpr uint8_t kNotificationEndpoint = 2;

  bool bulk_in_endpoint_allocated_ = false;

  usb_function_protocol_t protocol_;

  size_t config_ep_calls_ = 0;
  size_t disable_ep_calls_ = 0;
};

class FakeEthernetInterface : public ddk::EthernetIfcProtocol<FakeEthernetInterface> {
 public:
  FakeEthernetInterface()
      : protocol_({
            .ops = &ethernet_ifc_protocol_ops_,
            .ctx = this,
        }) {}

  void EthernetIfcStatus(uint32_t status) { last_status_ = status; }

  void EthernetIfcRecv(const void* data, size_t size, uint32_t flags) {
    sync_completion_signal(&packet_received_sync_);
  }

  const ethernet_ifc_protocol_t* Protocol() const { return &protocol_; }
  std::optional<uint32_t> LastStatus() const { return last_status_; }

  zx_status_t WaitUntilPacketReceived() {
    return sync_completion_wait_deadline(&packet_received_sync_, zx::time::infinite().get());
  }

 private:
  ethernet_ifc_protocol_t protocol_;
  std::optional<uint32_t> last_status_;
  sync_completion_t packet_received_sync_;
};

class RndisFunctionTest : public zxtest::Test {
 public:
  void SetUp() override {
    ddk_ = std::make_unique<fake_ddk::Bind>();

    {
      fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1](), 1);
      protocols[0] = {ZX_PROTOCOL_USB_FUNCTION,
                      {function_.Protocol()->ops, function_.Protocol()->ctx}};
      ddk_->SetProtocols(std::move(protocols));
    }

    ddk_->SetMetadata(mac_addr_.data(), mac_addr_.size());

    device_ = std::make_unique<RndisFunction>(/*parent=*/fake_ddk::FakeParent());
    device_->Bind();
  }

  void TearDown() override {
    auto device = device_.release();
    device->DdkAsyncRemove();
    ASSERT_OK(ddk_->WaitUntilRemove());
    device->DdkRelease();
    EXPECT_TRUE(ddk_->Ok());
  }

  static constexpr std::array<uint8_t, ETH_MAC_SIZE> mac_addr_ = {0x01, 0x23, 0x34,
                                                                  0x56, 0x67, 0x89};

  static constexpr size_t kNetbufSize =
      eth::BorrowedOperation<>::OperationSize(sizeof(ethernet_netbuf_t));

  void WriteCommand(const void* data, size_t length) {
    const usb_setup_t setup{
        .bmRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        .bRequest = USB_CDC_SEND_ENCAPSULATED_COMMAND,
        .wValue = 0,
        .wIndex = 0,
        .wLength = 0,
    };

    size_t actual;
    zx_status_t status =
        device_->UsbFunctionInterfaceControl(&setup, data, length, nullptr, 0, &actual);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(actual, 0);
  }

  void ReadResponse(void* data, size_t length) {
    const usb_setup_t setup{
        .bmRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        .bRequest = USB_CDC_GET_ENCAPSULATED_RESPONSE,
        .wValue = 0,
        .wIndex = 0,
        .wLength = 0,
    };

    size_t actual;
    zx_status_t status =
        device_->UsbFunctionInterfaceControl(&setup, nullptr, 0, data, length, &actual);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_GE(length, actual);
  }

  void QueryOid(uint32_t oid, void* data, size_t length, size_t* actual) {
    rndis_query query{
        .msg_type = RNDIS_QUERY_MSG,
        .msg_length = static_cast<uint32_t>(sizeof(rndis_query)),
        .request_id = 42,
        .oid = oid,
        .info_buffer_length = 0,
        .info_buffer_offset = 0,
        .reserved = 0,
    };
    WriteCommand(&query, sizeof(query));

    std::vector<uint8_t> buffer(sizeof(rndis_query_complete) + length);
    ReadResponse(buffer.data(), buffer.size());

    auto response = reinterpret_cast<rndis_query_complete*>(buffer.data());
    ASSERT_EQ(response->msg_type, RNDIS_QUERY_CMPLT);
    ASSERT_GE(response->msg_length, sizeof(rndis_query_complete));
    ASSERT_GE(response->request_id, 42);
    ASSERT_EQ(response->status, RNDIS_STATUS_SUCCESS);

    size_t offset = response->info_buffer_offset + offsetof(rndis_query_complete, request_id);
    ASSERT_GE(offset, sizeof(rndis_query_complete));
    ASSERT_LE(offset + response->info_buffer_length, buffer.size());

    memcpy(data, buffer.data() + offset, response->info_buffer_length);
    *actual = response->info_buffer_length;
  }

  void SetPacketFilter() {
    struct Payload {
      rndis_set header;
      uint8_t data[RNDIS_SET_INFO_BUFFER_LENGTH];
    } __PACKED;
    Payload set = {};

    uint32_t filter = 0;
    set.header.msg_type = RNDIS_SET_MSG;
    set.header.msg_length = static_cast<uint32_t>(sizeof(rndis_set) + sizeof(filter));
    set.header.request_id = 42;
    set.header.oid = OID_GEN_CURRENT_PACKET_FILTER;
    set.header.info_buffer_length = static_cast<uint32_t>(sizeof(filter));
    set.header.info_buffer_offset = sizeof(rndis_set) - offsetof(rndis_set, request_id);
    memcpy(&set.data, &filter, sizeof(filter));
    WriteCommand(&set, sizeof(set));

    rndis_indicate_status status;
    ReadResponse(&status, sizeof(status));
    EXPECT_EQ(status.msg_type, RNDIS_INDICATE_STATUS_MSG);
    EXPECT_EQ(status.msg_length, sizeof(rndis_indicate_status));
    EXPECT_EQ(status.status, RNDIS_STATUS_MEDIA_CONNECT);

    rndis_set_complete response;
    ReadResponse(&response, sizeof(response));
    ASSERT_EQ(response.msg_type, RNDIS_SET_CMPLT);
    ASSERT_GE(response.msg_length, sizeof(rndis_set_complete));
    ASSERT_GE(response.request_id, 42);
    ASSERT_EQ(response.status, RNDIS_STATUS_SUCCESS);
  }

  void ReadIndicateStatus(uint32_t expected_status) {
    rndis_indicate_status status;
    ReadResponse(&status, sizeof(status));
    ASSERT_EQ(status.msg_type, RNDIS_INDICATE_STATUS_MSG);
    ASSERT_EQ(status.msg_length, sizeof(rndis_indicate_status));
    ASSERT_EQ(status.status, expected_status);
  }

  std::unique_ptr<fake_ddk::Bind> ddk_;
  std::unique_ptr<RndisFunction> device_;
  FakeFunction function_;
  FakeEthernetInterface ifc_;
};

TEST_F(RndisFunctionTest, Suspend) {
  ddk::SuspendTxn txn(device_->zxdev(), 0, 0, 0);
  device_->DdkSuspend(std::move(txn));
  ddk_->WaitUntilSuspend();
}

TEST_F(RndisFunctionTest, Configure) {
  EXPECT_EQ(function_.ConfigEpCalls(), 0);
  EXPECT_EQ(function_.DisableEpCalls(), 0);

  zx_status_t status =
      device_->UsbFunctionInterfaceSetConfigured(/*configured=*/true, USB_SPEED_FULL);
  ASSERT_OK(status);

  EXPECT_EQ(function_.ConfigEpCalls(), 3);
  EXPECT_EQ(function_.DisableEpCalls(), 0);

  status = device_->UsbFunctionInterfaceSetConfigured(/*configured=*/false, USB_SPEED_FULL);
  ASSERT_OK(status);

  EXPECT_EQ(function_.ConfigEpCalls(), 3);
  EXPECT_EQ(function_.DisableEpCalls(), 3);
}

TEST_F(RndisFunctionTest, EthernetQuery) {
  ethernet_info_t info;
  zx_status_t status = device_->EthernetImplQuery(/*options=*/0, &info);
  ASSERT_OK(status);
  EXPECT_BYTES_EQ(&info.mac, mac_addr_.data(), mac_addr_.size());
}

TEST_F(RndisFunctionTest, EthernetStartStop) {
  zx_status_t status = device_->EthernetImplStart(ifc_.Protocol());
  ASSERT_OK(status);
  EXPECT_TRUE(ifc_.LastStatus().has_value());
  EXPECT_EQ(ifc_.LastStatus().value(), 0);

  status = device_->EthernetImplStart(ifc_.Protocol());
  EXPECT_EQ(status, ZX_ERR_ALREADY_BOUND);

  // Set a packet filter to put the device online.
  SetPacketFilter();
  EXPECT_TRUE(ifc_.LastStatus().has_value());
  EXPECT_EQ(ifc_.LastStatus().value(), ETHERNET_STATUS_ONLINE);

  device_->EthernetImplStop();
  status = device_->EthernetImplStart(ifc_.Protocol());
  ASSERT_OK(status);

  ReadIndicateStatus(RNDIS_STATUS_MEDIA_DISCONNECT);
}

TEST_F(RndisFunctionTest, InvalidSizeCommand) {
  std::vector<uint8_t> invalid_data = {0xa, 0xb};
  WriteCommand(invalid_data.data(), invalid_data.size());

  std::vector<uint8_t> buffer(sizeof(rndis_indicate_status) + sizeof(rndis_diagnostic_info) +
                              invalid_data.size());
  ReadResponse(buffer.data(), buffer.size());
  auto status = reinterpret_cast<rndis_indicate_status*>(buffer.data());
  EXPECT_EQ(status->msg_type, RNDIS_INDICATE_STATUS_MSG);
  EXPECT_EQ(status->msg_length, buffer.size());
  EXPECT_EQ(status->status, RNDIS_STATUS_INVALID_DATA);
}

TEST_F(RndisFunctionTest, InitMessage) {
  rndis_init msg{
      .msg_type = RNDIS_INITIALIZE_MSG,
      .msg_length = sizeof(rndis_init),
      .request_id = 42,
      .major_version = RNDIS_MAJOR_VERSION,
      .minor_version = RNDIS_MINOR_VERSION,
      .max_xfer_size = RNDIS_MAX_XFER_SIZE,
  };
  WriteCommand(&msg, sizeof(msg));

  rndis_init_complete response;
  ReadResponse(&response, sizeof(response));

  EXPECT_EQ(response.msg_type, RNDIS_INITIALIZE_CMPLT);
  EXPECT_EQ(response.msg_length, sizeof(response));
  EXPECT_EQ(response.request_id, 42);
  EXPECT_EQ(response.status, RNDIS_STATUS_SUCCESS);
  EXPECT_EQ(response.major_version, RNDIS_MAJOR_VERSION);
  EXPECT_EQ(response.minor_version, RNDIS_MINOR_VERSION);
  EXPECT_EQ(response.device_flags, RNDIS_DF_CONNECTIONLESS);
  EXPECT_EQ(response.medium, RNDIS_MEDIUM_802_3);
  EXPECT_EQ(response.max_packets_per_xfer, 1);
  EXPECT_EQ(response.max_xfer_size, RNDIS_MAX_XFER_SIZE);
  EXPECT_EQ(response.packet_alignment, 0);
  EXPECT_EQ(response.reserved0, 0);
  EXPECT_EQ(response.reserved1, 0);
}

TEST_F(RndisFunctionTest, Send) {
  // Start the interface and bring the device online.
  zx_status_t status = device_->EthernetImplStart(ifc_.Protocol());
  ASSERT_OK(status);

  SetPacketFilter();

  ethernet_info_t info;
  status = device_->EthernetImplQuery(/*options=*/0, &info);
  ASSERT_OK(status);

  uint32_t transmit_ok, transmit_errors, transmit_no_buffer;
  size_t actual;
  QueryOid(OID_GEN_RCV_OK, &transmit_ok, sizeof(transmit_ok), &actual);
  QueryOid(OID_GEN_RCV_ERROR, &transmit_errors, sizeof(transmit_errors), &actual);
  QueryOid(OID_GEN_RCV_NO_BUFFER, &transmit_no_buffer, sizeof(transmit_no_buffer), &actual);
  EXPECT_EQ(transmit_ok, 0);
  EXPECT_EQ(transmit_errors, 0);
  EXPECT_EQ(transmit_no_buffer, 0);

  // Fill the TX queue.
  for (size_t i = 0; i != 8; ++i) {
    auto buffer = eth::Operation<>::Alloc(info.netbuf_size);
    char data[] = "abcd";
    buffer->operation()->data_buffer = &data;
    buffer->operation()->data_size = sizeof(data);

    zx_status_t result;
    device_->EthernetImplQueueTx(/*options=*/0, buffer->take(),
                                 [](void* cookie, zx_status_t status, ethernet_netbuf_t* netbuf) {
                                   eth::Operation<> buffer(netbuf, kNetbufSize);
                                   *reinterpret_cast<zx_status_t*>(cookie) = status;
                                 },
                                 &result);
    EXPECT_OK(result);
  }

  QueryOid(OID_GEN_RCV_OK, &transmit_ok, sizeof(transmit_ok), &actual);
  QueryOid(OID_GEN_RCV_ERROR, &transmit_errors, sizeof(transmit_errors), &actual);
  QueryOid(OID_GEN_RCV_NO_BUFFER, &transmit_no_buffer, sizeof(transmit_no_buffer), &actual);
  EXPECT_EQ(transmit_ok, 8);
  EXPECT_EQ(transmit_errors, 0);
  EXPECT_EQ(transmit_no_buffer, 0);

  // One more packet should fail. The other packets haven't completed yet.
  {
    auto buffer = eth::Operation<>::Alloc(info.netbuf_size);
    char data[] = "abcd";
    buffer->operation()->data_buffer = &data;
    buffer->operation()->data_size = sizeof(data);

    zx_status_t result;
    device_->EthernetImplQueueTx(/*options=*/0, buffer->take(),
                                 [](void* cookie, zx_status_t status, ethernet_netbuf_t* netbuf) {
                                   eth::Operation<> buffer(netbuf, kNetbufSize);
                                   *reinterpret_cast<zx_status_t*>(cookie) = status;
                                 },
                                 &result);
    EXPECT_EQ(result, ZX_ERR_SHOULD_WAIT);
  }

  QueryOid(OID_GEN_RCV_OK, &transmit_ok, sizeof(transmit_ok), &actual);
  QueryOid(OID_GEN_RCV_ERROR, &transmit_errors, sizeof(transmit_errors), &actual);
  QueryOid(OID_GEN_RCV_NO_BUFFER, &transmit_no_buffer, sizeof(transmit_no_buffer), &actual);
  EXPECT_EQ(transmit_ok, 8);
  EXPECT_EQ(transmit_errors, 0);
  EXPECT_EQ(transmit_no_buffer, 1);

  // Drain the queue.
  function_.pending_in_requests_.CompleteAll(ZX_OK, 0);

  // We should be able to queue packets again, however usb requests are added back to the pool on
  // another thread so we should loop until at least one request has completed. We delay each
  // iteration with an exponential backoff to be polite.
  zx_status_t result;
  zx::duration delay = zx::msec(1);
  size_t attempts = 0;
  do {
    auto buffer = eth::Operation<>::Alloc(info.netbuf_size);
    char data[] = "abcd";
    buffer->operation()->data_buffer = &data;
    buffer->operation()->data_size = sizeof(data);

    device_->EthernetImplQueueTx(/*options=*/0, buffer->take(),
                                 [](void* cookie, zx_status_t status, ethernet_netbuf_t* netbuf) {
                                   eth::Operation<> buffer(netbuf, kNetbufSize);
                                   *reinterpret_cast<zx_status_t*>(cookie) = status;
                                 },
                                 &result);
    if (result != ZX_OK) {
      attempts += 1;
    }
    zx::nanosleep(zx::deadline_after(delay));
    delay = std::min(delay * 2, zx::sec(1));
  } while (result == ZX_ERR_SHOULD_WAIT);
  EXPECT_EQ(result, ZX_OK);

  QueryOid(OID_GEN_RCV_OK, &transmit_ok, sizeof(transmit_ok), &actual);
  QueryOid(OID_GEN_RCV_ERROR, &transmit_errors, sizeof(transmit_errors), &actual);
  QueryOid(OID_GEN_RCV_NO_BUFFER, &transmit_no_buffer, sizeof(transmit_no_buffer), &actual);
  EXPECT_EQ(transmit_ok, 9);
  EXPECT_EQ(transmit_errors, 0);
  EXPECT_EQ(transmit_no_buffer, 1 + attempts);
}

TEST_F(RndisFunctionTest, Receive) {
  // Start the interface and bring the device online.
  zx_status_t status = device_->EthernetImplStart(ifc_.Protocol());
  ASSERT_OK(status);

  SetPacketFilter();

  struct Payload {
    rndis_packet_header header;
    char data;
  };
  Payload payload = {};
  payload.header.msg_type = RNDIS_PACKET_MSG;
  payload.header.msg_length = sizeof(payload);
  payload.header.data_offset =
      sizeof(rndis_packet_header) - offsetof(rndis_packet_header, data_offset);
  payload.header.data_length = sizeof(payload.data);

  ASSERT_FALSE(function_.pending_out_requests_.is_empty());

  auto request = function_.pending_out_requests_.pop();
  ssize_t copied = request->CopyTo(&payload, sizeof(payload), 0);
  EXPECT_EQ(copied, sizeof(payload));

  request->Complete(ZX_OK, sizeof(payload));

  EXPECT_OK(ifc_.WaitUntilPacketReceived());
}

TEST_F(RndisFunctionTest, KeepAliveMessage) {
  rndis_header msg{
      msg.msg_type = RNDIS_KEEPALIVE_MSG,
      msg.msg_length = sizeof(rndis_header),
      msg.request_id = 42,
  };
  WriteCommand(&msg, sizeof(msg));

  rndis_header_complete response;
  ReadResponse(&response, sizeof(response));

  EXPECT_EQ(response.msg_type, RNDIS_KEEPALIVE_CMPLT);
  EXPECT_EQ(response.msg_length, sizeof(response));
  EXPECT_EQ(response.request_id, 42);
  EXPECT_EQ(response.status, RNDIS_STATUS_SUCCESS);
}

TEST_F(RndisFunctionTest, Halt) {
  zx_status_t status = device_->EthernetImplStart(ifc_.Protocol());
  ASSERT_OK(status);
  EXPECT_TRUE(ifc_.LastStatus().has_value());
  EXPECT_EQ(ifc_.LastStatus().value(), 0);

  status = device_->EthernetImplStart(ifc_.Protocol());
  EXPECT_EQ(status, ZX_ERR_ALREADY_BOUND);

  // Set a packet filter to put the device online.
  SetPacketFilter();
  EXPECT_TRUE(ifc_.LastStatus().has_value());
  EXPECT_EQ(ifc_.LastStatus().value(), ETHERNET_STATUS_ONLINE);

  rndis_header msg{
      msg.msg_type = RNDIS_HALT_MSG,
      msg.msg_length = sizeof(rndis_header),
      msg.request_id = 42,
  };
  WriteCommand(&msg, sizeof(msg));

  EXPECT_TRUE(ifc_.LastStatus().has_value());
  EXPECT_EQ(ifc_.LastStatus().value(), 0);

  EXPECT_EQ(function_.DisableEpCalls(), 3);
}

TEST_F(RndisFunctionTest, Reset) {
  zx_status_t status = device_->EthernetImplStart(ifc_.Protocol());
  ASSERT_OK(status);
  EXPECT_TRUE(ifc_.LastStatus().has_value());
  EXPECT_EQ(ifc_.LastStatus().value(), 0);

  status = device_->EthernetImplStart(ifc_.Protocol());
  EXPECT_EQ(status, ZX_ERR_ALREADY_BOUND);

  // Set a packet filter to put the device online.
  SetPacketFilter();
  EXPECT_TRUE(ifc_.LastStatus().has_value());
  EXPECT_EQ(ifc_.LastStatus().value(), ETHERNET_STATUS_ONLINE);

  rndis_header msg{
      msg.msg_type = RNDIS_RESET_MSG,
      msg.msg_length = sizeof(rndis_header),
      msg.request_id = 42,
  };
  WriteCommand(&msg, sizeof(msg));

  EXPECT_TRUE(ifc_.LastStatus().has_value());
  EXPECT_EQ(ifc_.LastStatus().value(), 0);

  rndis_reset_complete response;
  ReadResponse(&response, sizeof(response));
  EXPECT_EQ(response.msg_type, RNDIS_RESET_CMPLT);
  EXPECT_EQ(response.msg_length, sizeof(rndis_reset_complete));
  EXPECT_EQ(response.status, RNDIS_STATUS_SUCCESS);
}

TEST_F(RndisFunctionTest, OidSupportedList) {
  uint32_t supported_oids[100];
  size_t actual;
  QueryOid(OID_GEN_SUPPORTED_LIST, &supported_oids, sizeof(supported_oids), &actual);
  ASSERT_GE(actual, sizeof(uint32_t));
  ASSERT_EQ(actual % sizeof(uint32_t), 0);

  // Check that the list at least contains the list OID itself.
  bool contains_list_oid = false;
  for (size_t i = 0; i < actual / sizeof(uint32_t); ++i) {
    if (supported_oids[i] == OID_GEN_SUPPORTED_LIST) {
      contains_list_oid = true;
      break;
    }
  }
  EXPECT_TRUE(contains_list_oid);
}

TEST_F(RndisFunctionTest, OidHardwareStatus) {
  uint32_t hardware_status;
  size_t actual;
  QueryOid(OID_GEN_HARDWARE_STATUS, &hardware_status, sizeof(hardware_status), &actual);
  ASSERT_EQ(actual, sizeof(hardware_status));
  EXPECT_EQ(hardware_status, RNDIS_HW_STATUS_READY);
}

TEST_F(RndisFunctionTest, OidLinkSpeed) {
  zx_status_t status =
      device_->UsbFunctionInterfaceSetConfigured(/*configured=*/true, USB_SPEED_FULL);
  ASSERT_OK(status);

  uint32_t speed;
  size_t actual;
  QueryOid(OID_GEN_LINK_SPEED, &speed, sizeof(speed), &actual);
  ASSERT_EQ(actual, sizeof(speed));
  EXPECT_EQ(speed, 120'000);
}

TEST_F(RndisFunctionTest, OidMediaConnectStatus) {
  uint32_t status;
  size_t actual;
  QueryOid(OID_GEN_MEDIA_CONNECT_STATUS, &status, sizeof(status), &actual);
  ASSERT_EQ(actual, sizeof(status));
  EXPECT_EQ(status, RNDIS_STATUS_MEDIA_CONNECT);
}

TEST_F(RndisFunctionTest, OidPhysicalMedium) {
  uint32_t medium;
  size_t actual;
  QueryOid(OID_GEN_PHYSICAL_MEDIUM, &medium, sizeof(medium), &actual);
  ASSERT_EQ(actual, sizeof(medium));
  EXPECT_EQ(medium, RNDIS_MEDIUM_802_3);
}

TEST_F(RndisFunctionTest, OidMaximumSize) {
  uint32_t size;
  size_t actual;
  QueryOid(OID_GEN_MAXIMUM_TOTAL_SIZE, &size, sizeof(size), &actual);
  ASSERT_EQ(actual, sizeof(size));
  EXPECT_EQ(size, RNDIS_MAX_DATA_SIZE);
}

TEST_F(RndisFunctionTest, OidMacAddress) {
  std::array<uint8_t, ETH_MAC_SIZE> mac_addr;
  size_t actual;
  QueryOid(OID_802_3_PERMANENT_ADDRESS, mac_addr.data(), mac_addr.size(), &actual);
  ASSERT_EQ(actual, mac_addr.size());

  std::array<uint8_t, ETH_MAC_SIZE> expected = mac_addr_;
  expected[5] ^= 1;

  ASSERT_EQ(mac_addr, expected);
}

TEST_F(RndisFunctionTest, OidVendorDescription) {
  char description[100];
  size_t actual;
  QueryOid(OID_GEN_VENDOR_DESCRIPTION, &description, sizeof(description), &actual);
  EXPECT_STR_EQ(description, "Google");
}
