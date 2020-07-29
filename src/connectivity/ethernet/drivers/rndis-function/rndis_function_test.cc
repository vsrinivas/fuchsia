// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rndis_function.h"

#include <lib/fake_ddk/fake_ddk.h>

#include <ddktl/protocol/usb/function.h>
#include <zxtest/zxtest.h>

class FakeFunction : public ddk::UsbFunctionProtocol<FakeFunction, ddk::base_protocol> {
 public:
  FakeFunction() : protocol_({.ops = &usb_function_protocol_ops_, .ctx = this}) {}

  void CompleteAllRequests() {
    if (!pending_in_requests_.is_empty()) {
      pending_in_requests_.CompleteAll(ZX_ERR_IO_NOT_PRESENT, 0);
    }
    if (!pending_out_requests_.is_empty()) {
      pending_out_requests_.CompleteAll(ZX_ERR_IO_NOT_PRESENT, 0);
    }
    if (!pending_notification_requests_.is_empty()) {
      pending_notification_requests_.CompleteAll(ZX_ERR_IO_NOT_PRESENT, 0);
    }
  }

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

  zx_status_t UsbFunctionCancelAll(uint8_t ep_address) { return ZX_ERR_NOT_SUPPORTED; }

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

  void EthernetIfcRecv(const void* data, size_t size, uint32_t flags) { packets_received_ += 1; }

  const ethernet_ifc_protocol_t* Protocol() const { return &protocol_; }
  std::optional<uint32_t> LastStatus() const { return last_status_; }

  size_t PacketsReceived() const { return packets_received_; }

 private:
  ethernet_ifc_protocol_t protocol_;
  std::optional<uint32_t> last_status_;
  size_t packets_received_;
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
    function_.CompleteAllRequests();
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
        .msg_length = static_cast<uint32_t>(sizeof(rndis_query) + length),
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

  void SetOid(uint32_t oid, void* data, size_t length) {
    struct Payload {
      rndis_set header;
      uint8_t data[RNDIS_SET_INFO_BUFFER_LENGTH];
    } __PACKED;
    Payload set = {};

    set.header.msg_type = RNDIS_SET_MSG;
    set.header.msg_length = static_cast<uint32_t>(sizeof(rndis_set) + length);
    set.header.request_id = 42;
    set.header.oid = oid;
    set.header.info_buffer_length = static_cast<uint32_t>(length);
    set.header.info_buffer_offset = sizeof(rndis_set) - offsetof(rndis_set, request_id);
    if (data != nullptr) {
      memcpy(&set.data, data, length);
    }
    WriteCommand(&set, sizeof(set));

    rndis_set_complete response;
    ReadResponse(&response, sizeof(response));

    ASSERT_EQ(response.msg_type, RNDIS_SET_CMPLT);
    ASSERT_GE(response.msg_length, sizeof(rndis_set_complete));
    ASSERT_GE(response.request_id, 42);
    ASSERT_EQ(response.status, RNDIS_STATUS_SUCCESS);
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
  uint32_t filter = 0;
  SetOid(OID_GEN_CURRENT_PACKET_FILTER, &filter, sizeof(filter));
  EXPECT_TRUE(ifc_.LastStatus().has_value());
  EXPECT_EQ(ifc_.LastStatus().value(), ETHERNET_STATUS_ONLINE);

  device_->EthernetImplStop();
  status = device_->EthernetImplStart(ifc_.Protocol());
  ASSERT_OK(status);
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
  uint32_t filter = 0;
  SetOid(OID_GEN_CURRENT_PACKET_FILTER, &filter, sizeof(filter));

  ethernet_info_t info;
  status = device_->EthernetImplQuery(/*options=*/0, &info);
  ASSERT_OK(status);

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

  // Drain the queue.
  function_.pending_in_requests_.CompleteAll(ZX_OK, 0);

  // We should be able to queue packets again.
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
    EXPECT_EQ(result, ZX_OK);
  }
}

TEST_F(RndisFunctionTest, Receive) {
  // Start the interface and bring the device online.
  zx_status_t status = device_->EthernetImplStart(ifc_.Protocol());
  ASSERT_OK(status);
  uint32_t filter = 0;
  SetOid(OID_GEN_CURRENT_PACKET_FILTER, &filter, sizeof(filter));

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

  EXPECT_EQ(ifc_.PacketsReceived(), 1);
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
