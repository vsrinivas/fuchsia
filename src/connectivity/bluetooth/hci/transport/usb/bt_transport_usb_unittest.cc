// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bt_transport_usb.h"

#include <fuchsia/hardware/bt/hci/cpp/banjo.h>
#include <fuchsia/hardware/usb/cpp/banjo.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/thread_checker.h>
#include <lib/sync/condition.h>
#include <lib/sync/mutex.h>
#include <zircon/device/bt-hci.h>

#include <gtest/gtest.h>
#include <usb/request-cpp.h>

#include "lib/gtest/test_loop_fixture.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/devices/usb/testing/descriptor-builder/descriptor-builder.h"

namespace {

constexpr uint16_t kVendorId = 1;
constexpr uint16_t kProductId = 2;
constexpr size_t kInterruptPacketSize = 255u;
constexpr zx_duration_t kOutboundPacketWaitTimeout(zx::sec(30).get());

using Request = usb::Request<void>;
using UnownedRequest = usb::BorrowedRequest<void>;
using UnownedRequestQueue = usb::BorrowedRequestQueue<void>;

// The test fixture initializes bt-transport-usb as a child device of FakeUsbDevice.
// FakeUsbDevice implements the ddk::UsbProtocol template interface. ddk::UsbProtocol forwards USB
// static function calls to the methods of this class.
class FakeUsbDevice : public ddk::UsbProtocol<FakeUsbDevice> {
 public:
  FakeUsbDevice() = default;

  void set_device_descriptor(usb::DeviceDescriptorBuilder& dev_builder) {
    device_descriptor_data_ = dev_builder.Generate();
  }

  void ConfigureDefaultDescriptors() {
    ZX_ASSERT(thread_checker_.is_thread_valid());

    // Configure the USB endpoint configuration from Core Spec v5.3, Vol 4, Part B, Sec 2.1.1.

    // Interface 0 contains the bulk (ACL) and interrupt (HCI event) endpoints.
    // Endpoint indices are per direction (in/out).
    usb::InterfaceBuilder interface_0_builder(/*config_num=*/0);
    usb::EndpointBuilder bulk_in_endpoint_builder(/*config_num=*/0, USB_ENDPOINT_BULK,
                                                  /*endpoint_index=*/0, /*in=*/true);
    interface_0_builder.AddEndpoint(bulk_in_endpoint_builder);
    bulk_in_addr_ = usb::EpIndexToAddress(usb::kInEndpointStart);

    usb::EndpointBuilder bulk_out_endpoint_builder(/*config_num=*/0, USB_ENDPOINT_BULK,
                                                   /*endpoint_index=*/0, /*in=*/false);
    interface_0_builder.AddEndpoint(bulk_out_endpoint_builder);
    bulk_out_addr_ = usb::EpIndexToAddress(usb::kOutEndpointStart);

    usb::EndpointBuilder interrupt_endpoint_builder(/*config_num=*/0, USB_ENDPOINT_INTERRUPT,
                                                    /*endpoint_index=*/1, /*in=*/true);
    // The endpoint packet size must be large enough to send test packets.
    interrupt_endpoint_builder.set_max_packet_size(kInterruptPacketSize);
    interface_0_builder.AddEndpoint(interrupt_endpoint_builder);
    interrupt_addr_ = usb::EpIndexToAddress(usb::kInEndpointStart + 1);

    usb::ConfigurationBuilder config_builder(/*config_num=*/0);
    config_builder.AddInterface(interface_0_builder);

    usb::DeviceDescriptorBuilder dev_builder;
    dev_builder.set_vendor_id(kVendorId);
    dev_builder.set_product_id(kProductId);
    dev_builder.AddConfiguration(config_builder);
    set_device_descriptor(dev_builder);
  }

  void Unplug() {
    ZX_ASSERT(thread_checker_.is_thread_valid());

    UnownedRequestQueue requests;

    sync_mutex_lock(&mutex_);

    unplugged_ = true;

    // Complete all requests so that bt-transport-usb unbinds & releases successfully.
    while (!bulk_out_requests_.is_empty()) {
      requests.push(bulk_out_requests_.pop().value());
    }
    while (!bulk_in_requests_.is_empty()) {
      requests.push(bulk_in_requests_.pop().value());
    }
    while (!interrupt_requests_.is_empty()) {
      requests.push(interrupt_requests_.pop().value());
    }

    sync_mutex_unlock(&mutex_);
    requests.CompleteAll(ZX_ERR_IO_NOT_PRESENT, 0);
  }

  usb_protocol_t proto() const {
    usb_protocol_t proto;
    proto.ctx = const_cast<FakeUsbDevice*>(this);
    proto.ops = const_cast<usb_protocol_ops_t*>(&usb_protocol_ops_);
    return proto;
  }

  // Wait for the read thread to send n command packets. Returns the N packets.
  std::vector<std::vector<uint8_t>> wait_for_n_command_packets(size_t n) {
    std::vector<std::vector<uint8_t>> out;

    sync_mutex_lock(&mutex_);
    while (cmd_packets_.size() < n) {
      // Give up waiting after an arbitrary deadline to prevent tests timing out.
      zx_status_t status = sync_condition_timedwait(&cmd_packets_condition_, &mutex_,
                                                    zx_deadline_after(kOutboundPacketWaitTimeout));
      if (status == ZX_ERR_TIMED_OUT) {
        break;
      }
    }
    out = cmd_packets_;
    sync_mutex_unlock(&mutex_);
    return out;
  }

  // Wait for the read thread to send n ACL packets. Returns the N packets.
  std::vector<std::vector<uint8_t>> wait_for_n_acl_packets(size_t n) {
    std::vector<std::vector<uint8_t>> out;

    sync_mutex_lock(&mutex_);
    while (acl_packets_.size() < n) {
      // Give up waiting after an arbitrary deadline to prevent tests timing out.
      zx_status_t status = sync_condition_timedwait(&acl_packets_condition_, &mutex_,
                                                    zx_deadline_after(kOutboundPacketWaitTimeout));
      if (status == ZX_ERR_TIMED_OUT) {
        break;
      }
    }
    out = acl_packets_;
    sync_mutex_unlock(&mutex_);
    return out;
  }

  // ddk::UsbProtocol methods:

  // Called by bt-transport-usb to send command packets.
  // UsbControlOut may be called by the read thread.
  zx_status_t UsbControlOut(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                            int64_t timeout, const uint8_t* write_buffer, size_t write_size) {
    ZX_ASSERT(write_buffer);
    std::vector<uint8_t> buffer_copy(write_buffer, write_buffer + write_size);

    sync_mutex_lock(&mutex_);
    cmd_packets_.push_back(std::move(buffer_copy));
    sync_condition_signal(&cmd_packets_condition_);
    sync_mutex_unlock(&mutex_);
    return ZX_OK;
  }

  zx_status_t UsbControlIn(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                           int64_t timeout, uint8_t* out_read_buffer, size_t read_size,
                           size_t* out_read_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // UsbRequestQueue may be called from the read thread.
  void UsbRequestQueue(usb_request_t* usb_request,
                       const usb_request_complete_callback_t* complete_cb) {
    sync_mutex_lock(&mutex_);

    if (unplugged_) {
      sync_mutex_unlock(&mutex_);
      usb_request_complete(usb_request, ZX_ERR_IO_NOT_PRESENT, /*actual=*/0, complete_cb);
      return;
    }

    UnownedRequest request(usb_request, *complete_cb, sizeof(usb_request_t));

    if (request.request()->header.ep_address == bulk_in_addr_) {
      bulk_in_requests_.push(std::move(request));
      sync_mutex_unlock(&mutex_);
      return;
    }

    // If the request is for an ACL packet write, copy the data and complete the request.
    if (request.request()->header.ep_address == bulk_out_addr_) {
      std::vector<uint8_t> packet(request.request()->header.length);
      ssize_t actual_bytes_copied = request.CopyFrom(packet.data(), packet.size(), /*offset=*/0);
      EXPECT_EQ(actual_bytes_copied, static_cast<ssize_t>(packet.size()));
      acl_packets_.push_back(std::move(packet));
      sync_mutex_unlock(&mutex_);
      request.Complete(ZX_OK, /*actual=*/actual_bytes_copied);
      return;
    }

    if (request.request()->header.ep_address == interrupt_addr_) {
      interrupt_requests_.push(std::move(request));
      sync_mutex_unlock(&mutex_);
      return;
    }

    sync_mutex_unlock(&mutex_);
    zxlogf(ERROR, "FakeUsbDevice: received request for unknown endpoint");
    request.Complete(ZX_ERR_IO_NOT_PRESENT, /*actual=*/0);
  }

  usb_speed_t UsbGetSpeed() { return USB_SPEED_FULL; }

  zx_status_t UsbSetInterface(uint8_t interface_number, uint8_t alt_setting) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint8_t UsbGetConfiguration() { return 0; }

  zx_status_t UsbSetConfiguration(uint8_t configuration) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t UsbEnableEndpoint(const usb_endpoint_descriptor_t* ep_desc,
                                const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t UsbResetEndpoint(uint8_t ep_address) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t UsbResetDevice() { return ZX_ERR_NOT_SUPPORTED; }

  size_t UsbGetMaxTransferSize(uint8_t ep_address) { return 0; }

  uint32_t UsbGetDeviceId() { return 0; }
  void UsbGetDeviceDescriptor(usb_device_descriptor_t* out_desc) {
    ZX_ASSERT(thread_checker_.is_thread_valid());
    memcpy(out_desc, device_descriptor_data_.data(), sizeof(usb_device_descriptor_t));
  }

  zx_status_t UsbGetConfigurationDescriptorLength(uint8_t configuration, size_t* out_length) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t UsbGetConfigurationDescriptor(uint8_t configuration, uint8_t* out_desc_buffer,
                                            size_t desc_size, size_t* out_desc_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  size_t UsbGetDescriptorsLength() { return device_descriptor_data_.size(); }

  void UsbGetDescriptors(uint8_t* out_descs_buffer, size_t descs_size, size_t* out_descs_actual) {
    ZX_ASSERT(thread_checker_.is_thread_valid());
    ZX_ASSERT(descs_size == device_descriptor_data_.size());
    memcpy(out_descs_buffer, device_descriptor_data_.data(), descs_size);
  }

  zx_status_t UsbGetStringDescriptor(uint8_t desc_id, uint16_t lang_id, uint16_t* out_lang_id,
                                     uint8_t* out_string_buffer, size_t string_size,
                                     size_t* out_string_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t UsbCancelAll(uint8_t ep_address) { return ZX_ERR_NOT_SUPPORTED; }

  uint64_t UsbGetCurrentFrame() { return 0; }

  size_t UsbGetRequestSize() { return UnownedRequest::RequestSize(sizeof(usb_request_t)); }

  void StallOneBulkInRequest() {
    sync_mutex_lock(&mutex_);

    std::optional<usb::BorrowedRequest<>> value = bulk_in_requests_.pop();
    sync_mutex_unlock(&mutex_);

    if (!value) {
      return;
    }
    value->Complete(ZX_ERR_IO_INVALID, /*actual=*/0);
  }

  // Sends 1 ACL data packet.
  // Returns true if a response was sent.
  bool SendOneBulkInResponse(std::vector<uint8_t> buffer) {
    sync_mutex_lock(&mutex_);
    std::optional<usb::BorrowedRequest<>> req = bulk_in_requests_.pop();
    sync_mutex_unlock(&mutex_);

    if (!req) {
      return false;
    }

    // Copy data into the request's VMO. The request must have been allocated with a large enough
    // VMO (usb_request_alloc's data_size parameter). bt-transport-usb currently uses the max ACL
    // frame size for the data_size.
    ssize_t actual_copy_size = req->CopyTo(buffer.data(), buffer.size(), /*offset=*/0);
    EXPECT_EQ(actual_copy_size, static_cast<ssize_t>(buffer.size()));
    // This calls the request callback and sets response.status and response.actual.
    req->Complete(ZX_OK, /*actual=*/actual_copy_size);

    return true;
  }

  // The first even chunk buffer should be at least 2 bytes, and must specify an accurate
  // parameter_total_size (second byte).
  // Returns true if a response was sent.
  bool SendHciEvent(const std::vector<uint8_t>& buffer) {
    sync_mutex_lock(&mutex_);
    std::optional<usb::BorrowedRequest<>> req = interrupt_requests_.pop();
    sync_mutex_unlock(&mutex_);

    if (!req) {
      return false;
    }

    // Copy data into the request's VMO. The request must have been allocated with a large enough
    // VMO (usb_request_alloc's data_size parameter).
    ssize_t actual_copy_size = req->CopyTo(buffer.data(), buffer.size(), /*offset=*/0);
    EXPECT_EQ(actual_copy_size, static_cast<ssize_t>(buffer.size()));
    // This calls the request callback and sets response.status and response.actual.
    req->Complete(ZX_OK, /*actual=*/actual_copy_size);

    return true;
  }

 private:
  fit::thread_checker thread_checker_;

  // This mutex guards members that are accessed in USB methods that may be invoked by the read
  // thread in bt-transport-usb.
  sync_mutex_t mutex_;

  bool unplugged_ __TA_GUARDED(mutex_) = false;

  // Command packets received from bt-transport-usb.
  std::vector<std::vector<uint8_t>> cmd_packets_ __TA_GUARDED(mutex_);
  // Condition signaled when a command packet is added to cmd_packets_
  sync_condition_t cmd_packets_condition_;

  // Outbound ACL packets received from bt-transport-usb.
  std::vector<std::vector<uint8_t>> acl_packets_ __TA_GUARDED(mutex_);
  // Condition signaled when a ACL packet is added to acl_packets_
  sync_condition_t acl_packets_condition_;

  // ACL data in/out requests.
  UnownedRequestQueue bulk_in_requests_ __TA_GUARDED(mutex_);
  UnownedRequestQueue bulk_out_requests_ __TA_GUARDED(mutex_);

  // Requests for HCI events
  UnownedRequestQueue interrupt_requests_ __TA_GUARDED(mutex_);

  std::vector<uint8_t> device_descriptor_data_;
  std::optional<uint8_t> bulk_in_addr_;
  std::optional<uint8_t> bulk_out_addr_;
  std::optional<uint8_t> interrupt_addr_;
};

class BtTransportUsbTest : public ::gtest::TestLoopFixture {
 public:
  void SetUp() override {
    root_device_ = MockDevice::FakeRootParent();
    root_device_->AddProtocol(ZX_PROTOCOL_USB, fake_usb_device_.proto().ops,
                              fake_usb_device_.proto().ctx);

    fake_usb_device_.ConfigureDefaultDescriptors();

    // bt-transport-usb doesn't use ctx.
    ASSERT_EQ(bt_transport_usb::Device::Create(/*ctx=*/nullptr, root_device_.get()), ZX_OK);
    ASSERT_EQ(1u, root_device()->child_count());
    ASSERT_TRUE(dut());
  }

  void TearDown() override {
    // Complete USB requests with error status. This must be done before releasing DUT so that
    // release doesn't block on request completion.
    fake_usb_device_.Unplug();
    // DUT should call device_async_remove() in response to failed USB requests.
    EXPECT_EQ(dut()->WaitUntilAsyncRemoveCalled(), ZX_OK);

    dut()->UnbindOp();
    EXPECT_TRUE(dut()->UnbindReplyCalled());
    dut()->ReleaseOp();
  }

  // The root device that bt-transport-usb binds to.
  MockDevice* root_device() const { return root_device_.get(); }

  // Returns the MockDevice corresponding to the bt-transport-usb driver.
  MockDevice* dut() const { return root_device_->GetLatestChild(); }

  FakeUsbDevice* fake_usb() { return &fake_usb_device_; }

 private:
  std::shared_ptr<MockDevice> root_device_;
  FakeUsbDevice fake_usb_device_;
};

class BtTransportUsbHciProtocolTest : public BtTransportUsbTest {
 public:
  void SetUp() override {
    BtTransportUsbTest::SetUp();

    bt_transport_usb::Device* dev = dut()->GetDeviceContext<bt_transport_usb::Device>();
    ASSERT_NE(dev, nullptr);
    ASSERT_EQ(dev->DdkGetProtocol(ZX_PROTOCOL_BT_HCI, &hci_proto_), ZX_OK);

    zx::channel cmd_chan_driver_end;
    ASSERT_EQ(zx::channel::create(/*flags=*/0, &cmd_chan_, &cmd_chan_driver_end), ZX_OK);
    bt_hci_open_command_channel(&hci_proto_, cmd_chan_driver_end.release());

    // Configure wait for readable signal on command channel.
    cmd_chan_readable_wait_.set_object(cmd_chan_.get());
    zx_status_t wait_begin_status = cmd_chan_readable_wait_.Begin(dispatcher());
    ASSERT_EQ(wait_begin_status, ZX_OK) << zx_status_get_string(wait_begin_status);

    zx::channel acl_chan_driver_end;
    ASSERT_EQ(zx::channel::create(/*flags=*/0, &acl_chan_, &acl_chan_driver_end), ZX_OK);
    bt_hci_open_acl_data_channel(&hci_proto_, acl_chan_driver_end.release());

    // Configure wait for readable signal on ACL channel.
    acl_chan_readable_wait_.set_object(acl_chan_.get());
    wait_begin_status = acl_chan_readable_wait_.Begin(dispatcher());
    ASSERT_EQ(wait_begin_status, ZX_OK) << zx_status_get_string(wait_begin_status);

    configure_snoop_channel();
  }

  void TearDown() override {
    cmd_chan_readable_wait_.Cancel();
    cmd_chan_.reset();
    acl_chan_.reset();
    snoop_chan_.reset();

    BtTransportUsbTest::TearDown();
  }

  zx_status_t configure_snoop_channel() {
    snoop_chan_readable_wait_.Cancel();
    zx::channel snoop_chan_driver_end;
    ZX_ASSERT(zx::channel::create(/*flags=*/0, &snoop_chan_, &snoop_chan_driver_end) == ZX_OK);
    zx_status_t status = bt_hci_open_snoop_channel(&hci_proto_, snoop_chan_driver_end.release());
    if (status == ZX_OK) {
      // Configure wait for readable signal on snoop channel.
      snoop_chan_readable_wait_.set_object(snoop_chan_.get());
      zx_status_t wait_begin_status = snoop_chan_readable_wait_.Begin(dispatcher());
      ZX_ASSERT_MSG(wait_begin_status == ZX_OK, "snoop wait begin failed: %s",
                    zx_status_get_string(wait_begin_status));
    }
    return status;
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
  // Since bt-transport-usb writes to the channel on the same thread as tests, this handler is
  // dispatched immediately when the channel is written to.
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
      ADD_FAILURE();
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

  bt_hci_protocol_t hci_proto_;

  zx::channel cmd_chan_;
  zx::channel acl_chan_;
  zx::channel snoop_chan_;

  async::WaitMethod<BtTransportUsbHciProtocolTest, &BtTransportUsbHciProtocolTest::OnChannelReady>
      cmd_chan_readable_wait_{this, zx_handle_t(), ZX_CHANNEL_READABLE};
  async::WaitMethod<BtTransportUsbHciProtocolTest, &BtTransportUsbHciProtocolTest::OnChannelReady>
      snoop_chan_readable_wait_{this, zx_handle_t(), ZX_CHANNEL_READABLE};
  async::WaitMethod<BtTransportUsbHciProtocolTest, &BtTransportUsbHciProtocolTest::OnChannelReady>
      acl_chan_readable_wait_{this, zx_handle_t(), ZX_CHANNEL_READABLE};

  std::vector<std::vector<uint8_t>> cmd_chan_received_packets_;
  std::vector<std::vector<uint8_t>> snoop_chan_received_packets_;
  std::vector<std::vector<uint8_t>> acl_chan_received_packets_;
};

// This tests the test fixture setup and teardown.
TEST_F(BtTransportUsbTest, Lifecycle) {}

TEST_F(BtTransportUsbTest, IgnoresStalledRequest) {
  fake_usb()->StallOneBulkInRequest();
  EXPECT_FALSE(dut()->RemoveCalled());
}

TEST_F(BtTransportUsbTest, Name) { EXPECT_EQ(std::string(dut()->name()), "bt_transport_usb"); }

TEST_F(BtTransportUsbTest, Properties) {
  cpp20::span<const zx_device_prop_t> props = dut()->GetProperties();
  ASSERT_EQ(props.size(), 3u);
  EXPECT_EQ(props[0].id, BIND_PROTOCOL);
  EXPECT_EQ(props[0].value, ZX_PROTOCOL_BT_TRANSPORT);
  EXPECT_EQ(props[1].id, BIND_USB_VID);
  EXPECT_EQ(props[1].value, kVendorId);
  EXPECT_EQ(props[2].id, BIND_USB_PID);
  EXPECT_EQ(props[2].value, kProductId);
}

TEST(BtTransportUsbBindFailureTest, NoConfigurationDescriptor) {
  FakeUsbDevice fake_usb_device;
  std::shared_ptr<MockDevice> root_device = MockDevice::FakeRootParent();
  root_device->AddProtocol(ZX_PROTOCOL_USB, fake_usb_device.proto().ops,
                           fake_usb_device.proto().ctx);
  EXPECT_EQ(bt_transport_usb::Device::Create(/*ctx=*/nullptr, root_device.get()),
            ZX_ERR_NOT_SUPPORTED);
}

TEST(BtTransportUsbBindFailureTest, ConfigurationDescriptorWithoutInterfaces) {
  FakeUsbDevice fake_usb_device;
  std::shared_ptr<MockDevice> root_device = MockDevice::FakeRootParent();
  root_device->AddProtocol(ZX_PROTOCOL_USB, fake_usb_device.proto().ops,
                           fake_usb_device.proto().ctx);

  usb::ConfigurationBuilder config_builder(/*config_num=*/0);
  usb::DeviceDescriptorBuilder dev_builder;
  dev_builder.AddConfiguration(config_builder);
  fake_usb_device.set_device_descriptor(dev_builder);

  EXPECT_EQ(bt_transport_usb::Device::Create(/*ctx=*/nullptr, root_device.get()),
            ZX_ERR_NOT_SUPPORTED);
}

TEST(BtTransportUsbBindFailureTest, ConfigurationDescriptorWithIncorrectNumberOfEndpoints) {
  FakeUsbDevice fake_usb_device;
  std::shared_ptr<MockDevice> root_device = MockDevice::FakeRootParent();
  root_device->AddProtocol(ZX_PROTOCOL_USB, fake_usb_device.proto().ops,
                           fake_usb_device.proto().ctx);

  usb::InterfaceBuilder interface_builder(/*config_num=*/0);
  usb::EndpointBuilder interrupt_endpoint_builder(/*config_num=*/0, USB_ENDPOINT_INTERRUPT,
                                                  /*endpoint_index=*/1, /*in=*/true);
  interface_builder.AddEndpoint(interrupt_endpoint_builder);
  usb::ConfigurationBuilder config_builder(/*config_num=*/0);
  config_builder.AddInterface(interface_builder);
  usb::DeviceDescriptorBuilder dev_builder;
  dev_builder.AddConfiguration(config_builder);
  fake_usb_device.set_device_descriptor(dev_builder);

  EXPECT_EQ(bt_transport_usb::Device::Create(/*ctx=*/nullptr, root_device.get()),
            ZX_ERR_NOT_SUPPORTED);
}

TEST(BtTransportUsbBindFailureTest, ConfigurationDescriptorWithIncorrectEndpointTypesInInterface0) {
  FakeUsbDevice fake_usb_device;
  std::shared_ptr<MockDevice> root_device = MockDevice::FakeRootParent();
  root_device->AddProtocol(ZX_PROTOCOL_USB, fake_usb_device.proto().ops,
                           fake_usb_device.proto().ctx);

  usb::InterfaceBuilder interface_0_builder(/*config_num=*/0);
  usb::EndpointBuilder interrupt_endpoint_builder(/*config_num=*/0, USB_ENDPOINT_INTERRUPT,
                                                  /*endpoint_index=*/0, /*in=*/true);
  interface_0_builder.AddEndpoint(interrupt_endpoint_builder);
  usb::EndpointBuilder bulk_in_endpoint_builder(/*config_num=*/0, USB_ENDPOINT_BULK,
                                                /*endpoint_index=*/1, /*in=*/true);
  interface_0_builder.AddEndpoint(bulk_in_endpoint_builder);

  // Add isoc endpoint instead of expected bulk out endpoint.
  usb::EndpointBuilder bulk_out_endpoint_builder(/*config_num=*/0, USB_ENDPOINT_ISOCHRONOUS,
                                                 /*endpoint_index=*/0, /*in=*/false);
  interface_0_builder.AddEndpoint(bulk_out_endpoint_builder);

  usb::ConfigurationBuilder config_builder(/*config_num=*/0);
  config_builder.AddInterface(interface_0_builder);

  usb::DeviceDescriptorBuilder dev_builder;
  dev_builder.AddConfiguration(config_builder);
  fake_usb_device.set_device_descriptor(dev_builder);

  EXPECT_EQ(bt_transport_usb::Device::Create(/*ctx=*/nullptr, root_device.get()),
            ZX_ERR_NOT_SUPPORTED);
}

TEST_F(BtTransportUsbHciProtocolTest, ReceiveManySmallHciEvents) {
  std::vector<uint8_t> kSnoopEventBuffer = {
      BT_HCI_SNOOP_TYPE_EVT | BT_HCI_SNOOP_FLAG_RECV,  // snoop packet flag
      0x01,                                            // arbitrary event code
      0x01,                                            // parameter_total_size
      0x02                                             // arbitrary parameter
  };
  std::vector<uint8_t> kEventBuffer(kSnoopEventBuffer.begin() + 1, kSnoopEventBuffer.end());
  const int kNumEvents = 50;

  for (int i = 0; i < kNumEvents; i++) {
    ASSERT_TRUE(fake_usb()->SendHciEvent(kEventBuffer));
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

TEST_F(BtTransportUsbHciProtocolTest, ReceiveManyHciEventsSplitIntoTwoResponses) {
  const std::vector<uint8_t> kSnoopEventBuffer = {
      BT_HCI_SNOOP_TYPE_EVT | BT_HCI_SNOOP_FLAG_RECV,  // Snoop packet flag
      0x01,                                            // event code
      0x02,                                            // parameter_total_size
      0x03,                                            // arbitrary parameter
      0x04                                             // arbitrary parameter
  };
  const std::vector<uint8_t> kEventBuffer(kSnoopEventBuffer.begin() + 1, kSnoopEventBuffer.end());
  const std::vector<uint8_t> kPart1(kEventBuffer.begin(), kEventBuffer.begin() + 3);
  const std::vector<uint8_t> kPart2(kEventBuffer.begin() + 3, kEventBuffer.end());

  const int kNumEvents = 50;
  for (int i = 0; i < kNumEvents; i++) {
    EXPECT_TRUE(fake_usb()->SendHciEvent(kPart1));
    EXPECT_TRUE(fake_usb()->SendHciEvent(kPart2));
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

TEST_F(BtTransportUsbHciProtocolTest, SendHciCommands) {
  const std::vector<uint8_t> kSnoopCmd0 = {
      BT_HCI_SNOOP_TYPE_CMD,  // Snoop packet flag
      0x00,                   // arbitrary payload
  };
  const std::vector<uint8_t> kCmd0(kSnoopCmd0.begin() + 1, kSnoopCmd0.end());
  zx_status_t write_status =
      cmd_chan()->write(/*flags=*/0, kCmd0.data(), static_cast<uint32_t>(kCmd0.size()),
                        /*handles=*/nullptr,
                        /*num_handles=*/0);
  EXPECT_EQ(write_status, ZX_OK);

  const std::vector<uint8_t> kSnoopCmd1 = {
      BT_HCI_SNOOP_TYPE_CMD,  // Snoop packet flag
      0x01,                   // arbitrary payload
  };
  const std::vector<uint8_t> kCmd1(kSnoopCmd1.begin() + 1, kSnoopCmd1.end());
  write_status = cmd_chan()->write(/*flags=*/0, kCmd1.data(), static_cast<uint32_t>(kCmd1.size()),
                                   /*handles=*/nullptr,
                                   /*num_handles=*/0);
  EXPECT_EQ(write_status, ZX_OK);

  std::vector<std::vector<uint8_t>> packets = fake_usb()->wait_for_n_command_packets(2);
  ASSERT_EQ(packets.size(), 2u);
  EXPECT_EQ(packets[0], kCmd0);
  EXPECT_EQ(packets[1], kCmd1);

  RunLoopUntilIdle();
  ASSERT_EQ(snoop_packets().size(), 2u);
  EXPECT_EQ(snoop_packets()[0], kSnoopCmd0);
  EXPECT_EQ(snoop_packets()[1], kSnoopCmd1);
}

TEST_F(BtTransportUsbHciProtocolTest, ReceiveManyAclPackets) {
  const std::vector<uint8_t> kSnoopAclBuffer = {
      BT_HCI_SNOOP_TYPE_ACL | BT_HCI_SNOOP_FLAG_RECV,  // Snoop packet flag
      0x04, 0x05                                       // arbitrary payload
  };
  const std::vector<uint8_t> kAclBuffer(kSnoopAclBuffer.begin() + 1, kSnoopAclBuffer.end());

  const int kNumPackets = 50;
  for (int i = 0; i < kNumPackets; i++) {
    EXPECT_TRUE(fake_usb()->SendOneBulkInResponse(kAclBuffer));
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

TEST_F(BtTransportUsbHciProtocolTest, SendManyAclPackets) {
  const uint8_t kNumPackets = 25;
  for (uint8_t i = 0; i < kNumPackets; i++) {
    const std::vector<uint8_t> kAclPacket = {i};
    zx_status_t write_status =
        acl_chan()->write(/*flags=*/0, kAclPacket.data(), static_cast<uint32_t>(kAclPacket.size()),
                          /*handles=*/nullptr,
                          /*num_handles=*/0);
    ASSERT_EQ(write_status, ZX_OK);
  }

  std::vector<std::vector<uint8_t>> packets = fake_usb()->wait_for_n_acl_packets(kNumPackets);
  ASSERT_EQ(packets.size(), kNumPackets);
  for (uint8_t i = 0; i < kNumPackets; i++) {
    EXPECT_EQ(packets[i], std::vector<uint8_t>{i});
  }

  RunLoopUntilIdle();
  ASSERT_EQ(snoop_packets().size(), kNumPackets);
  for (uint8_t i = 0; i < kNumPackets; i++) {
    const std::vector<uint8_t> kExpectedSnoopPacket = {BT_HCI_SNOOP_TYPE_ACL,  // Snoop packet flag
                                                       i};
    EXPECT_EQ(snoop_packets()[i], kExpectedSnoopPacket);
  }
}

TEST_F(BtTransportUsbHciProtocolTest, ReconfigureSnoopChannelMultipleTimesSucceeds) {
  EXPECT_EQ(configure_snoop_channel(), ZX_OK);
  EXPECT_EQ(configure_snoop_channel(), ZX_OK);
}

}  // namespace
