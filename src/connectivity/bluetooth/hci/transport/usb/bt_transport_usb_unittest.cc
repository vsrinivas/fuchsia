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
constexpr uint16_t kScoMaxPacketSize = 255 + 3;  // payload + 3 bytes header
constexpr uint8_t kEventAndAclInterfaceNum = 0u;
constexpr uint8_t kIsocInterfaceNum = 1u;
constexpr zx_duration_t kOutboundPacketWaitTimeout(zx::sec(30).get());
constexpr zx_duration_t kOutboundPacketWaitExpectedToFailTimeout(zx::sec(1).get());

using Request = usb::Request<void>;
using UnownedRequest = usb::BorrowedRequest<void>;
using UnownedRequestQueue = usb::BorrowedRequestQueue<void>;

// The test fixture initializes bt-transport-usb as a child device of FakeUsbDevice.
// FakeUsbDevice implements the ddk::UsbProtocol template interface. ddk::UsbProtocol forwards USB
// static function calls to the methods of this class.
class FakeUsbDevice : public ddk::UsbProtocol<FakeUsbDevice> {
 public:
  explicit FakeUsbDevice(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void set_device_descriptor(usb::DeviceDescriptorBuilder& dev_builder) {
    device_descriptor_data_ = dev_builder.Generate();
  }

  void set_config_descriptor(usb::ConfigurationBuilder& config_builder) {
    config_descriptor_data_ = config_builder.Generate();
  }

  void ConfigureDefaultDescriptors(bool with_sco = true) {
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

    if (with_sco) {
      for (uint8_t alt_setting = 0; alt_setting < 6; alt_setting++) {
        usb::InterfaceBuilder interface_1_builder(/*config_num=*/0, alt_setting);
        usb::EndpointBuilder isoc_out_endpoint_builder(/*config_num=*/0, USB_ENDPOINT_ISOCHRONOUS,
                                                       /*endpoint_index=*/1, /*in=*/false);
        isoc_out_endpoint_builder.set_max_packet_size(kScoMaxPacketSize);
        interface_1_builder.AddEndpoint(isoc_out_endpoint_builder);
        isoc_out_addr_ = usb::EpIndexToAddress(usb::kOutEndpointStart + 1);

        usb::EndpointBuilder isoc_in_endpoint_builder(/*config_num=*/0, USB_ENDPOINT_ISOCHRONOUS,
                                                      /*endpoint_index=*/2, /*in=*/true);
        isoc_in_endpoint_builder.set_max_packet_size(kScoMaxPacketSize);
        interface_1_builder.AddEndpoint(isoc_in_endpoint_builder);
        isoc_in_addr_ = usb::EpIndexToAddress(usb::kInEndpointStart + 2);
        config_builder.AddInterface(interface_1_builder);
      }
    }
    set_config_descriptor(config_builder);

    usb::DeviceDescriptorBuilder dev_builder;
    dev_builder.set_vendor_id(kVendorId);
    dev_builder.set_product_id(kProductId);
    dev_builder.AddConfiguration(config_builder);
    set_device_descriptor(dev_builder);
  }

  void Unplug() {
    ZX_ASSERT(thread_checker_.is_thread_valid());

    sync_mutex_lock(&mutex_);

    unplugged_ = true;

    // All requests should have completed or been canceled before unplugging the USB device.
    ZX_ASSERT(bulk_out_requests_.is_empty());
    ZX_ASSERT(bulk_in_requests_.is_empty());
    ZX_ASSERT(interrupt_requests_.is_empty());
    ZX_ASSERT(isoc_in_requests_.is_empty());

    sync_mutex_unlock(&mutex_);
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

  // Wait for the read thread to send n SCO packets. Returns the N packets.
  std::vector<std::vector<uint8_t>> wait_for_n_sco_packets(
      size_t n, zx_duration_t timeout = kOutboundPacketWaitTimeout) {
    std::vector<std::vector<uint8_t>> out;

    sync_mutex_lock(&mutex_);
    while (sco_packets_.size() < n) {
      // Give up waiting after an arbitrary deadline to prevent tests timing out.
      zx_status_t status =
          sync_condition_timedwait(&sco_packets_condition_, &mutex_, zx_deadline_after(timeout));
      if (status == ZX_ERR_TIMED_OUT) {
        break;
      }
    }
    out = sco_packets_;
    sync_mutex_unlock(&mutex_);
    return out;
  }

  bool interrupt_enabled() {
    sync_mutex_lock(&mutex_);
    bool out = interrupt_enabled_;
    sync_mutex_unlock(&mutex_);
    return out;
  }

  bool bulk_in_enabled() {
    sync_mutex_lock(&mutex_);
    bool out = bulk_in_enabled_;
    sync_mutex_unlock(&mutex_);
    return out;
  }

  bool bulk_out_enabled() {
    sync_mutex_lock(&mutex_);
    bool out = bulk_out_enabled_;
    sync_mutex_unlock(&mutex_);
    return out;
  }

  bool isoc_in_enabled() {
    sync_mutex_lock(&mutex_);
    bool out = isoc_in_enabled_;
    sync_mutex_unlock(&mutex_);
    return out;
  }

  bool isoc_out_enabled() {
    sync_mutex_lock(&mutex_);
    bool out = isoc_out_enabled_;
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
      async::PostTask(dispatcher_, [usb_request, complete_cb] {
        usb_request_complete(usb_request, ZX_ERR_IO_NOT_PRESENT, /*actual=*/0, complete_cb);
      });
      return;
    }

    UnownedRequest request(usb_request, *complete_cb, sizeof(usb_request_t));

    if (request.request()->header.ep_address == bulk_in_addr_) {
      ZX_ASSERT(bulk_in_enabled_);
      bulk_in_requests_.push(std::move(request));
      sync_mutex_unlock(&mutex_);
      return;
    }

    // If the request is for an ACL packet write, copy the data and complete the request.
    if (request.request()->header.ep_address == bulk_out_addr_) {
      ZX_ASSERT(bulk_out_enabled_);
      std::vector<uint8_t> packet(request.request()->header.length);
      ssize_t actual_bytes_copied = request.CopyFrom(packet.data(), packet.size(), /*offset=*/0);
      EXPECT_EQ(actual_bytes_copied, static_cast<ssize_t>(packet.size()));
      acl_packets_.push_back(std::move(packet));
      sync_condition_signal(&acl_packets_condition_);
      sync_mutex_unlock(&mutex_);
      async::PostTask(dispatcher_, [request = std::move(request), actual_bytes_copied]() mutable {
        request.Complete(ZX_OK, /*actual=*/actual_bytes_copied);
      });
      return;
    }

    if (isoc_in_addr_ && request.request()->header.ep_address == *isoc_in_addr_) {
      ZX_ASSERT(isoc_in_enabled_);
      ZX_ASSERT_MSG(isoc_interface_alt_ != 0,
                    "requests must not be sent to isoc interface with alt setting 0");
      isoc_in_requests_.push(std::move(request));
      sync_mutex_unlock(&mutex_);
      return;
    }

    if (isoc_out_addr_ && request.request()->header.ep_address == *isoc_out_addr_) {
      ZX_ASSERT(isoc_out_enabled_);
      ZX_ASSERT_MSG(isoc_interface_alt_ != 0,
                    "requests must not be sent to isoc interface with alt setting 0");
      std::vector<uint8_t> packet(request.request()->header.length);
      ssize_t actual_bytes_copied = request.CopyFrom(packet.data(), packet.size(), /*offset=*/0);
      EXPECT_EQ(actual_bytes_copied, static_cast<ssize_t>(packet.size()));
      sco_packets_.push_back(std::move(packet));
      sync_condition_signal(&sco_packets_condition_);
      sync_mutex_unlock(&mutex_);
      async::PostTask(dispatcher_, [request = std::move(request), actual_bytes_copied]() mutable {
        request.Complete(ZX_OK, /*actual=*/actual_bytes_copied);
      });
      return;
    }

    if (request.request()->header.ep_address == interrupt_addr_) {
      ZX_ASSERT(interrupt_enabled_);
      interrupt_requests_.push(std::move(request));
      sync_mutex_unlock(&mutex_);
      return;
    }

    sync_mutex_unlock(&mutex_);
    zxlogf(ERROR, "FakeUsbDevice: received request for unknown endpoint");
    async::PostTask(dispatcher_, [request = std::move(request)]() mutable {
      request.Complete(ZX_ERR_IO_NOT_PRESENT, /*actual=*/0);
    });
  }

  usb_speed_t UsbGetSpeed() { return USB_SPEED_FULL; }

  zx_status_t UsbSetInterface(uint8_t interface_number, uint8_t alt_setting) {
    sync_mutex_lock(&mutex_);
    if (interface_number == kEventAndAclInterfaceNum) {
      ZX_ASSERT(alt_setting == 0);
      event_and_acl_interface_set_count_++;
      sync_mutex_unlock(&mutex_);
      return ZX_OK;
    }
    if (interface_number == kIsocInterfaceNum) {
      // Endpoints must be disabled before changing the interface.
      ZX_ASSERT(!isoc_in_enabled_);
      ZX_ASSERT(!isoc_out_enabled_);
      isoc_interface_alt_ = alt_setting;
      isoc_interface_set_count_++;
      sync_mutex_unlock(&mutex_);
      return ZX_OK;
    }
    sync_mutex_unlock(&mutex_);
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint8_t UsbGetConfiguration() { return 0; }

  zx_status_t UsbSetConfiguration(uint8_t configuration) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t UsbEnableEndpoint(const usb_endpoint_descriptor_t* ep_desc,
                                const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable) {
    sync_mutex_lock(&mutex_);

    if (interrupt_addr_ && interrupt_addr_.value() == ep_desc->b_endpoint_address) {
      interrupt_enabled_ = enable;
      sync_mutex_unlock(&mutex_);
      return ZX_OK;
    }

    if (bulk_in_addr_ && bulk_in_addr_.value() == ep_desc->b_endpoint_address) {
      bulk_in_enabled_ = enable;
      sync_mutex_unlock(&mutex_);
      return ZX_OK;
    }

    if (bulk_out_addr_ && bulk_out_addr_.value() == ep_desc->b_endpoint_address) {
      bulk_out_enabled_ = enable;
      sync_mutex_unlock(&mutex_);
      return ZX_OK;
    }

    if (isoc_in_addr_ && isoc_in_addr_.value() == ep_desc->b_endpoint_address) {
      isoc_in_enabled_ = enable;
      sync_mutex_unlock(&mutex_);
      return ZX_OK;
    }

    if (isoc_out_addr_ && isoc_out_addr_.value() == ep_desc->b_endpoint_address) {
      isoc_out_enabled_ = enable;
      sync_mutex_unlock(&mutex_);
      return ZX_OK;
    }

    sync_mutex_unlock(&mutex_);
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
    *out_length = config_descriptor_data_.size();
    return ZX_OK;
  }

  zx_status_t UsbGetConfigurationDescriptor(uint8_t configuration, uint8_t* out_desc_buffer,
                                            size_t desc_size, size_t* out_desc_actual) {
    if (desc_size != config_descriptor_data_.size()) {
      return ZX_ERR_INVALID_ARGS;
    }
    memcpy(out_desc_buffer, config_descriptor_data_.data(), config_descriptor_data_.size());
    *out_desc_actual = config_descriptor_data_.size();
    return ZX_OK;
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

  zx_status_t UsbCancelAll(uint8_t ep_address) {
    sync_mutex_lock(&mutex_);

    if (bulk_in_addr_ && ep_address == *bulk_in_addr_) {
      auto requests = std::move(bulk_in_requests_);
      sync_mutex_unlock(&mutex_);
      requests.CompleteAll(ZX_ERR_CANCELED, 0);
      return ZX_OK;
    }
    if (bulk_out_addr_ && ep_address == *bulk_out_addr_) {
      auto requests = std::move(bulk_out_requests_);
      sync_mutex_unlock(&mutex_);
      requests.CompleteAll(ZX_ERR_CANCELED, 0);
      return ZX_OK;
    }
    if (interrupt_addr_ && ep_address == *interrupt_addr_) {
      auto requests = std::move(interrupt_requests_);
      sync_mutex_unlock(&mutex_);
      requests.CompleteAll(ZX_ERR_CANCELED, 0);
      return ZX_OK;
    }

    if (isoc_in_addr_ && isoc_in_addr_.value() == ep_address) {
      isoc_in_canceled_count_++;
      UnownedRequestQueue isoc_in_reqs = std::move(isoc_in_requests_);
      sync_mutex_unlock(&mutex_);
      isoc_in_reqs.CompleteAll(ZX_ERR_CANCELED, 0);
      return ZX_OK;
    }

    if (isoc_out_addr_ && isoc_out_addr_.value() == ep_address) {
      isoc_out_canceled_count_++;
      // There's nothing to cancel because isoc out requests are completed immediately.
      sync_mutex_unlock(&mutex_);
      return ZX_OK;
    }

    sync_mutex_unlock(&mutex_);
    return ZX_ERR_NOT_SUPPORTED;
  }

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

  void StallOneIsocInRequest() {
    sync_mutex_lock(&mutex_);

    std::optional<usb::BorrowedRequest<>> value = isoc_in_requests_.pop();
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

  // Sends 1 SCO data packet.
  // Returns true if a response was sent.
  bool SendOneIsocInResponse(std::vector<uint8_t> buffer) {
    sync_mutex_lock(&mutex_);
    std::optional<usb::BorrowedRequest<>> req = isoc_in_requests_.pop();
    sync_mutex_unlock(&mutex_);

    if (!req) {
      return false;
    }

    // Copy data into the request's VMO. The request must have been allocated with a large enough
    // VMO (usb_request_alloc's data_size parameter). bt-transport-usb currently uses the max SCO
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

  uint8_t isoc_interface_alt() {
    sync_mutex_lock(&mutex_);
    uint8_t out = isoc_interface_alt_;
    sync_mutex_unlock(&mutex_);
    return out;
  }

  int isoc_interface_set_count() {
    sync_mutex_lock(&mutex_);
    int out = isoc_interface_set_count_;
    sync_mutex_unlock(&mutex_);
    return out;
  }

 private:
  fit::thread_checker thread_checker_;

  async_dispatcher_t* dispatcher_;

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

  // Outbound SCO packets received from bt-transport-usb.
  std::vector<std::vector<uint8_t>> sco_packets_ __TA_GUARDED(mutex_);
  // Condition signaled when a SCO packet is added to sco_packets_
  sync_condition_t sco_packets_condition_;

  // ACL data in/out requests.
  UnownedRequestQueue bulk_in_requests_ __TA_GUARDED(mutex_);
  UnownedRequestQueue bulk_out_requests_ __TA_GUARDED(mutex_);

  // Requests for HCI events
  UnownedRequestQueue interrupt_requests_ __TA_GUARDED(mutex_);

  // Inbound SCO requests.
  UnownedRequestQueue isoc_in_requests_ __TA_GUARDED(mutex_);

  std::vector<uint8_t> device_descriptor_data_;
  std::vector<uint8_t> config_descriptor_data_;
  std::optional<uint8_t> bulk_in_addr_;
  bool bulk_in_enabled_ __TA_GUARDED(mutex_) = false;
  std::optional<uint8_t> bulk_out_addr_;
  bool bulk_out_enabled_ __TA_GUARDED(mutex_) = false;
  std::optional<uint8_t> isoc_out_addr_;
  bool isoc_out_enabled_ __TA_GUARDED(mutex_) = false;
  int isoc_out_canceled_count_ __TA_GUARDED(mutex_) = 0;
  std::optional<uint8_t> isoc_in_addr_;
  bool isoc_in_enabled_ __TA_GUARDED(mutex_) = false;
  int isoc_in_canceled_count_ __TA_GUARDED(mutex_) = 0;
  std::optional<uint8_t> interrupt_addr_;
  bool interrupt_enabled_ __TA_GUARDED(mutex_) = false;
  uint8_t isoc_interface_alt_ __TA_GUARDED(mutex_) = 0;
  int isoc_interface_set_count_ __TA_GUARDED(mutex_) = 0;
  int event_and_acl_interface_set_count_ __TA_GUARDED(mutex_) = 0;
};

class BtTransportUsbTest : public ::gtest::TestLoopFixture {
 public:
  void SetUp() override {
    fake_usb_device_.emplace(dispatcher());
    root_device_ = MockDevice::FakeRootParent();
    root_device_->AddProtocol(ZX_PROTOCOL_USB, fake_usb_device_->proto().ops,
                              fake_usb_device_->proto().ctx);

    fake_usb_device_->ConfigureDefaultDescriptors();

    // bt-transport-usb doesn't use ctx.
    ASSERT_EQ(bt_transport_usb::Device::Create(/*ctx=*/nullptr, root_device_.get()), ZX_OK);
    ASSERT_EQ(1u, root_device()->child_count());
    ASSERT_TRUE(dut());
    EXPECT_TRUE(fake_usb_device_->interrupt_enabled());
    EXPECT_TRUE(fake_usb_device_->bulk_in_enabled());
    EXPECT_TRUE(fake_usb_device_->bulk_out_enabled());
    EXPECT_FALSE(fake_usb_device_->isoc_in_enabled());
    EXPECT_FALSE(fake_usb_device_->isoc_out_enabled());
  }

  void TearDown() override {
    RunLoopUntilIdle();

    dut()->UnbindOp();
    EXPECT_EQ(dut()->WaitUntilUnbindReplyCalled(), ZX_OK);
    EXPECT_FALSE(fake_usb_device_->interrupt_enabled());
    EXPECT_FALSE(fake_usb_device_->bulk_in_enabled());
    EXPECT_FALSE(fake_usb_device_->bulk_out_enabled());
    EXPECT_FALSE(fake_usb_device_->isoc_in_enabled());
    EXPECT_FALSE(fake_usb_device_->isoc_out_enabled());

    dut()->ReleaseOp();

    fake_usb_device_->Unplug();
  }

  // The root device that bt-transport-usb binds to.
  MockDevice* root_device() const { return root_device_.get(); }

  // Returns the MockDevice corresponding to the bt-transport-usb driver.
  MockDevice* dut() const { return root_device_->GetLatestChild(); }

  FakeUsbDevice* fake_usb() { return &fake_usb_device_.value(); }

 private:
  std::shared_ptr<MockDevice> root_device_;
  std::optional<FakeUsbDevice> fake_usb_device_;
};

class BtTransportUsbHciProtocolTest : public BtTransportUsbTest {
 public:
  void SetUp() override {
    BtTransportUsbTest::SetUp();

    bt_transport_usb::Device* dev = dut()->GetDeviceContext<bt_transport_usb::Device>();
    ASSERT_NE(dev, nullptr);
    ASSERT_EQ(dev->DdkGetProtocol(ZX_PROTOCOL_BT_HCI, &hci_proto_), ZX_OK);

    ASSERT_EQ(zx::channel::create(/*flags=*/0, &cmd_chan_, &cmd_chan_driver_end_), ZX_OK);

    // Configure wait for readable signal on command channel.
    cmd_chan_readable_wait_.set_object(cmd_chan_.get());
    zx_status_t wait_begin_status = cmd_chan_readable_wait_.Begin(dispatcher());
    ASSERT_EQ(wait_begin_status, ZX_OK) << zx_status_get_string(wait_begin_status);

    ASSERT_EQ(zx::channel::create(/*flags=*/0, &acl_chan_, &acl_chan_driver_end_), ZX_OK);

    // Configure wait for readable signal on ACL channel.
    acl_chan_readable_wait_.set_object(acl_chan_.get());
    wait_begin_status = acl_chan_readable_wait_.Begin(dispatcher());
    ASSERT_EQ(wait_begin_status, ZX_OK) << zx_status_get_string(wait_begin_status);

    ASSERT_EQ(zx::channel::create(/*flags=*/0, &sco_chan_, &sco_chan_driver_end_), ZX_OK);

    // Configure wait for readable signal on SCO channel.
    sco_chan_readable_wait_.set_object(sco_chan_.get());
    wait_begin_status = sco_chan_readable_wait_.Begin(dispatcher());
    ASSERT_EQ(wait_begin_status, ZX_OK) << zx_status_get_string(wait_begin_status);
  }

  void TearDown() override {
    cmd_chan_readable_wait_.Cancel();
    cmd_chan_.reset();
    acl_chan_.reset();
    snoop_chan_.reset();

    BtTransportUsbTest::TearDown();
  }

  // Starts the driver.  Must be called before we expect any result from the driver.
  // Used to queue multiple packets on a channel before the driver read thread starts.
  void ConnectDriver() {
    // connect the snoop channel first, as some channels may have packets waiting.
    configure_snoop_channel();
    bt_hci_open_command_channel(&hci_proto_, cmd_chan_driver_end_.release());
    bt_hci_open_acl_data_channel(&hci_proto_, acl_chan_driver_end_.release());
    zx_status_t sco_status = bt_hci_open_sco_channel(&hci_proto_, sco_chan_driver_end_.release());
    ASSERT_EQ(sco_status, ZX_OK);
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

  const std::vector<std::vector<uint8_t>>& received_sco_packets() const {
    return sco_chan_received_packets_;
  }

  zx::channel* cmd_chan() { return &cmd_chan_; }

  zx::channel* acl_chan() { return &acl_chan_; }

  zx::channel* sco_chan() { return &sco_chan_; }

  zx_status_t ConfigureSco(sco_coding_format_t coding_format, sco_encoding_t encoding,
                           sco_sample_rate_t sample_rate) {
    zx_status_t status = ZX_OK;
    // The callback is called synchronously.
    bt_hci_configure_sco(
        &hci_proto_, coding_format, encoding, sample_rate,
        [](void* status, zx_status_t s) { *reinterpret_cast<zx_status_t*>(status) = s; }, &status);
    return status;
  }

  zx_status_t ResetSco() {
    zx_status_t status = ZX_OK;
    // The callback is called synchronously.
    bt_hci_reset_sco(
        &hci_proto_,
        [](void* status, zx_status_t s) { *reinterpret_cast<zx_status_t*>(status) = s; }, &status);
    return status;
  }

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
    } else if (wait == &sco_chan_readable_wait_) {
      chan = zx::unowned_channel(sco_chan_);
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
      } else if (wait == &sco_chan_readable_wait_) {
        sco_chan_received_packets_.push_back(std::move(bytes));
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
  zx::channel sco_chan_;
  zx::channel snoop_chan_;
  // The driver ends of the above channels.  Passed to the driver on ConnectDriver(), and afterwards
  // are ZX_HANDLE_INVALID.  snoop_chan_ is not included as it's connected with
  // configure_snoop_channel and we never write to the client end.
  zx::channel cmd_chan_driver_end_;
  zx::channel acl_chan_driver_end_;
  zx::channel sco_chan_driver_end_;

  async::WaitMethod<BtTransportUsbHciProtocolTest, &BtTransportUsbHciProtocolTest::OnChannelReady>
      cmd_chan_readable_wait_{this, zx_handle_t(), ZX_CHANNEL_READABLE};
  async::WaitMethod<BtTransportUsbHciProtocolTest, &BtTransportUsbHciProtocolTest::OnChannelReady>
      snoop_chan_readable_wait_{this, zx_handle_t(), ZX_CHANNEL_READABLE};
  async::WaitMethod<BtTransportUsbHciProtocolTest, &BtTransportUsbHciProtocolTest::OnChannelReady>
      acl_chan_readable_wait_{this, zx_handle_t(), ZX_CHANNEL_READABLE};
  async::WaitMethod<BtTransportUsbHciProtocolTest, &BtTransportUsbHciProtocolTest::OnChannelReady>
      sco_chan_readable_wait_{this, zx_handle_t(), ZX_CHANNEL_READABLE};

  std::vector<std::vector<uint8_t>> cmd_chan_received_packets_;
  std::vector<std::vector<uint8_t>> snoop_chan_received_packets_;
  std::vector<std::vector<uint8_t>> acl_chan_received_packets_;
  std::vector<std::vector<uint8_t>> sco_chan_received_packets_;
};

class BtTransportUsbBindFailureTest : public ::gtest::TestLoopFixture {};

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

TEST_F(BtTransportUsbBindFailureTest, NoConfigurationDescriptor) {
  FakeUsbDevice fake_usb_device(dispatcher());
  std::shared_ptr<MockDevice> root_device = MockDevice::FakeRootParent();
  root_device->AddProtocol(ZX_PROTOCOL_USB, fake_usb_device.proto().ops,
                           fake_usb_device.proto().ctx);
  EXPECT_EQ(bt_transport_usb::Device::Create(/*ctx=*/nullptr, root_device.get()),
            ZX_ERR_NOT_SUPPORTED);
}

TEST_F(BtTransportUsbBindFailureTest, ConfigurationDescriptorWithoutInterfaces) {
  FakeUsbDevice fake_usb_device(dispatcher());
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

TEST_F(BtTransportUsbBindFailureTest, ConfigurationDescriptorWithIncorrectNumberOfEndpoints) {
  FakeUsbDevice fake_usb_device(dispatcher());
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

TEST_F(BtTransportUsbBindFailureTest,
       ConfigurationDescriptorWithIncorrectEndpointTypesInInterface0) {
  FakeUsbDevice fake_usb_device(dispatcher());
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
  ConnectDriver();
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
  ConnectDriver();
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
      0x01,                   // arbitrary payload (longer than before)
      0xC0,
      0xDE,
  };
  const std::vector<uint8_t> kCmd1(kSnoopCmd1.begin() + 1, kSnoopCmd1.end());
  write_status = cmd_chan()->write(/*flags=*/0, kCmd1.data(), static_cast<uint32_t>(kCmd1.size()),
                                   /*handles=*/nullptr,
                                   /*num_handles=*/0);
  EXPECT_EQ(write_status, ZX_OK);

  // Delayed connect to the driver, so the HCI CMD read loop must process both of the above commands
  // at once.
  ConnectDriver();
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
  ConnectDriver();
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
  const uint8_t kNumPackets = 8;
  for (uint8_t i = 0; i < kNumPackets; i++) {
    std::vector<uint8_t> packet;
    // Vary the length of the packets (start small)
    if (i % 2) {
      packet = std::vector<uint8_t>(1, i);
    } else {
      packet = std::vector<uint8_t>(10, i);
    }
    zx_status_t write_status =
        acl_chan()->write(/*flags=*/0, packet.data(), static_cast<uint32_t>(packet.size()),
                          /*handles=*/nullptr,
                          /*num_handles=*/0);
    ASSERT_EQ(write_status, ZX_OK);
  }

  // Delayed connect to the driver, so that the driver must process many packets at once.
  ConnectDriver();
  std::vector<std::vector<uint8_t>> packets = fake_usb()->wait_for_n_acl_packets(kNumPackets);
  // Ensure completion callbacks are called.
  RunLoopUntilIdle();
  ASSERT_EQ(packets.size(), kNumPackets);
  for (uint8_t i = 0; i < kNumPackets; i++) {
    if (i % 2) {
      EXPECT_EQ(packets[i], std::vector<uint8_t>(1, i));
    } else {
      EXPECT_EQ(packets[i], std::vector<uint8_t>(10, i));
    }
  }

  RunLoopUntilIdle();
  ASSERT_EQ(snoop_packets().size(), kNumPackets);
  for (uint8_t i = 0; i < kNumPackets; i++) {
    std::vector<uint8_t> expectedSnoopPacket = {BT_HCI_SNOOP_TYPE_ACL};
    if (i % 2) {
      const std::vector<uint8_t> data(1, i);
      expectedSnoopPacket.insert(expectedSnoopPacket.end(), data.begin(), data.end());
    } else {
      const std::vector<uint8_t> data(10, i);
      expectedSnoopPacket.insert(expectedSnoopPacket.end(), data.begin(), data.end());
    }
    EXPECT_EQ(snoop_packets()[i], expectedSnoopPacket);
  }
}

class BtTransportUsbScoNotSupportedTest : public ::gtest::TestLoopFixture {};
TEST_F(BtTransportUsbScoNotSupportedTest, OpenScoChannel) {
  // Create USB device without a SCO interface.
  FakeUsbDevice fake_usb_device(dispatcher());
  fake_usb_device.ConfigureDefaultDescriptors(/*with_sco=*/false);

  std::shared_ptr<MockDevice> root_device = MockDevice::FakeRootParent();
  root_device->AddProtocol(ZX_PROTOCOL_USB, fake_usb_device.proto().ops,
                           fake_usb_device.proto().ctx);

  // Binding should succeed.
  ASSERT_EQ(bt_transport_usb::Device::Create(/*ctx=*/nullptr, root_device.get()), ZX_OK);
  MockDevice* mock_dev = root_device->GetLatestChild();
  bt_transport_usb::Device* dev = mock_dev->GetDeviceContext<bt_transport_usb::Device>();
  ASSERT_NE(dev, nullptr);

  bt_hci_protocol_t hci_proto;
  ASSERT_EQ(dev->DdkGetProtocol(ZX_PROTOCOL_BT_HCI, &hci_proto), ZX_OK);

  zx::channel chan;
  EXPECT_EQ(bt_hci_open_sco_channel(&hci_proto, chan.get()), ZX_ERR_NOT_SUPPORTED);
  zx_status_t configure_status = ZX_OK;
  bt_hci_configure_sco(
      &hci_proto, SCO_CODING_FORMAT_MSBC, SCO_ENCODING_BITS_16, SCO_SAMPLE_RATE_KHZ_16,
      [](void* status, zx_status_t s) { *reinterpret_cast<zx_status_t*>(status) = s; },
      &configure_status);
  EXPECT_EQ(configure_status, ZX_ERR_NOT_SUPPORTED);

  mock_dev->UnbindOp();
  EXPECT_EQ(mock_dev->WaitUntilUnbindReplyCalled(), ZX_OK);
  mock_dev->ReleaseOp();
  fake_usb_device.Unplug();
}

TEST_F(BtTransportUsbHciProtocolTest, ConfigureScoAltSettings) {
  ConnectDriver();
  EXPECT_EQ(fake_usb()->isoc_interface_set_count(), 1);
  EXPECT_EQ(ZX_OK,
            ConfigureSco(SCO_CODING_FORMAT_MSBC, SCO_ENCODING_BITS_16, SCO_SAMPLE_RATE_KHZ_16));
  EXPECT_EQ(fake_usb()->isoc_interface_alt(), 1);
  EXPECT_EQ(fake_usb()->isoc_interface_set_count(), 2);
  EXPECT_EQ(ZX_OK,
            ConfigureSco(SCO_CODING_FORMAT_CVSD, SCO_ENCODING_BITS_8, SCO_SAMPLE_RATE_KHZ_8));
  EXPECT_EQ(fake_usb()->isoc_interface_alt(), 1);
  EXPECT_EQ(fake_usb()->isoc_interface_set_count(), 2);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED,
            ConfigureSco(SCO_CODING_FORMAT_CVSD, SCO_ENCODING_BITS_8, SCO_SAMPLE_RATE_KHZ_16));
  EXPECT_EQ(fake_usb()->isoc_interface_set_count(), 2);
  EXPECT_EQ(ZX_OK,
            ConfigureSco(SCO_CODING_FORMAT_CVSD, SCO_ENCODING_BITS_16, SCO_SAMPLE_RATE_KHZ_8));
  EXPECT_EQ(fake_usb()->isoc_interface_alt(), 2);
  EXPECT_EQ(fake_usb()->isoc_interface_set_count(), 3);
  EXPECT_EQ(ZX_OK,
            ConfigureSco(SCO_CODING_FORMAT_CVSD, SCO_ENCODING_BITS_16, SCO_SAMPLE_RATE_KHZ_16));
  EXPECT_EQ(fake_usb()->isoc_interface_alt(), 4);
  EXPECT_EQ(fake_usb()->isoc_interface_set_count(), 4);
}

TEST_F(BtTransportUsbHciProtocolTest, SendManyScoPackets) {
  ConnectDriver();
  ConfigureSco(SCO_CODING_FORMAT_CVSD, SCO_ENCODING_BITS_8, SCO_SAMPLE_RATE_KHZ_8);
  EXPECT_EQ(fake_usb()->isoc_interface_alt(), 1);

  const uint8_t kNumPackets = 8;
  for (uint8_t i = 0; i < kNumPackets; i++) {
    const std::vector<uint8_t> kScoPacket = {i};
    zx_status_t write_status =
        sco_chan()->write(/*flags=*/0, kScoPacket.data(), static_cast<uint32_t>(kScoPacket.size()),
                          /*handles=*/nullptr,
                          /*num_handles=*/0);
    ASSERT_EQ(write_status, ZX_OK);
  }

  std::vector<std::vector<uint8_t>> packets = fake_usb()->wait_for_n_sco_packets(kNumPackets);
  // Complete requests.
  RunLoopUntilIdle();
  ASSERT_EQ(packets.size(), kNumPackets);
  for (uint8_t i = 0; i < kNumPackets; i++) {
    EXPECT_EQ(packets[i], std::vector<uint8_t>{i});
  }

  RunLoopUntilIdle();
  ASSERT_EQ(snoop_packets().size(), kNumPackets);
  for (uint8_t i = 0; i < kNumPackets; i++) {
    const std::vector<uint8_t> kExpectedSnoopPacket = {BT_HCI_SNOOP_TYPE_SCO,  // Snoop packet flag
                                                       i};
    EXPECT_EQ(snoop_packets()[i], kExpectedSnoopPacket);
  }

  ResetSco();
  EXPECT_EQ(fake_usb()->isoc_interface_alt(), 0);
}

TEST_F(BtTransportUsbHciProtocolTest, QueueManyScoPacketsDueToNoAltSettingSelected) {
  ConnectDriver();
  EXPECT_EQ(fake_usb()->isoc_interface_alt(), 0);

  const uint8_t kNumPackets = 8;
  for (uint8_t i = 0; i < kNumPackets; i++) {
    const std::vector<uint8_t> kScoPacket = {i};
    zx_status_t write_status =
        sco_chan()->write(/*flags=*/0, kScoPacket.data(), static_cast<uint32_t>(kScoPacket.size()),
                          /*handles=*/nullptr,
                          /*num_handles=*/0);
    ASSERT_EQ(write_status, ZX_OK);
  }
  // This wait should time out since no packets should be sent.
  // TODO(fxbug.dev/88491): Using a timeout is undesirable in tests, so this should be replaced if
  // the driver is migrated to libasync.
  std::vector<std::vector<uint8_t>> packets =
      fake_usb()->wait_for_n_sco_packets(1, kOutboundPacketWaitExpectedToFailTimeout);
  // Complete any requests.
  RunLoopUntilIdle();
  ASSERT_EQ(packets.size(), 0u);

  // Set an alt setting. Buffered packets should be sent.
  ConfigureSco(SCO_CODING_FORMAT_CVSD, SCO_ENCODING_BITS_8, SCO_SAMPLE_RATE_KHZ_8);
  packets = fake_usb()->wait_for_n_sco_packets(kNumPackets, kOutboundPacketWaitTimeout);
  ASSERT_EQ(packets.size(), kNumPackets);
}

TEST_F(BtTransportUsbHciProtocolTest, ReceiveManyScoPackets) {
  ConnectDriver();
  ConfigureSco(SCO_CODING_FORMAT_CVSD, SCO_ENCODING_BITS_8, SCO_SAMPLE_RATE_KHZ_8);

  const std::vector<uint8_t> kSnoopScoBuffer = {
      BT_HCI_SNOOP_TYPE_SCO | BT_HCI_SNOOP_FLAG_RECV,  // Snoop packet flag
      0x01,                                            // arbitrary header fields
      0x02,
      0x03,  // payload length
      0x04,  // arbitrary payload
      0x05,
      0x06,
  };
  const std::vector<uint8_t> kScoBuffer(kSnoopScoBuffer.begin() + 1, kSnoopScoBuffer.end());
  // Split the packet into 2 chunks to test recombination.
  const std::vector<uint8_t> kScoBufferChunk0(kScoBuffer.begin(), kScoBuffer.begin() + 4);
  const std::vector<uint8_t> kScoBufferChunk1(kScoBuffer.begin() + 4, kScoBuffer.end());

  const int kNumPackets = 25;
  for (int i = 0; i < kNumPackets; i++) {
    EXPECT_TRUE(fake_usb()->SendOneIsocInResponse(kScoBufferChunk0));
    EXPECT_TRUE(fake_usb()->SendOneIsocInResponse(kScoBufferChunk1));
    RunLoopUntilIdle();
  }

  ASSERT_EQ(received_sco_packets().size(), static_cast<size_t>(kNumPackets));
  for (const std::vector<uint8_t>& packet : received_sco_packets()) {
    EXPECT_EQ(packet.size(), kScoBuffer.size());
    EXPECT_EQ(packet, kScoBuffer);
  }

  RunLoopUntilIdle();
  ASSERT_EQ(snoop_packets().size(), static_cast<size_t>(kNumPackets));
  for (const std::vector<uint8_t>& packet : snoop_packets()) {
    EXPECT_EQ(packet, kSnoopScoBuffer);
  }
}

TEST_F(BtTransportUsbHciProtocolTest, IgnoresStalledScoRequest) {
  ConnectDriver();
  ConfigureSco(SCO_CODING_FORMAT_CVSD, SCO_ENCODING_BITS_8, SCO_SAMPLE_RATE_KHZ_8);
  fake_usb()->StallOneIsocInRequest();
  EXPECT_FALSE(dut()->RemoveCalled());
}

}  // namespace
