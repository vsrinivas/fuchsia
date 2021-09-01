// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../usb-device.h"

#include <fidl/fuchsia.hardware.usb.device/cpp/wire.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fit/function.h>

#include <thread>

#include <fbl/ref_counted.h>
#include <usb/request-cpp.h>
#include <utf_conversion/utf_conversion.h>
#include <zxtest/zxtest.h>

#include "lib/ddk/driver.h"

namespace usb_bus {

template <typename T, size_t N>
constexpr T MakeConstant(const char value[N]) {
  T retval = 0;
  for (T i = 0; i < N; i++) {
    retval = static_cast<T>(retval | (static_cast<T>(value[i]) << (i * 8)));
  }
  static_assert(N <= sizeof(T));
  return retval;
}

constexpr uint8_t kVendorId = 81;
constexpr uint8_t kProductId = 35;
constexpr uint8_t kDeviceClass = 2;
constexpr uint8_t kDeviceSubclass = 6;
constexpr uint8_t kDeviceProtocol = 250;
constexpr uint32_t kDeviceId = 42;
constexpr uint32_t kHubId = 32;
constexpr uint32_t kMaxTransferSize = 9001;
constexpr uint8_t kTransferSizeEndpoint = 5;
constexpr uint64_t kCurrentFrame = MakeConstant<uint64_t, 7>("fuchsia");
constexpr size_t kRequestSize = 256;
const char16_t* kStringDescriptors[][2] = {{u"Fuchsia", u"Fucsia"}, {u"Device", u"Dispositivo"}};

constexpr usb_speed_t kDeviceSpeed = MakeConstant<usb_speed_t, 4>("slow");

class Binder : public fake_ddk::Bind {
 public:
  bool get_remove_called() { return remove_called_; }
};

class FakeHci : public ddk::UsbHciProtocol<FakeHci> {
 public:
  FakeHci() {
    proto_.ops = &usb_hci_protocol_ops_;
    proto_.ctx = this;
  }
  uint64_t UsbHciGetCurrentFrame() { return kCurrentFrame; }

  zx_status_t UsbHciConfigureHub(uint32_t device_id, usb_speed_t speed,
                                 const usb_hub_descriptor_t* desc, bool multi_tt) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t UsbHciHubDeviceAdded(uint32_t device_id, uint32_t port, usb_speed_t speed) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t UsbHciHubDeviceRemoved(uint32_t device_id, uint32_t port) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t UsbHciHubDeviceReset(uint32_t device_id, uint32_t port) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t UsbHciResetEndpoint(uint32_t device_id, uint8_t ep_address) {
    if (device_id == kDeviceId) {
      reset_endpoint_ = ep_address;
    }
    return ZX_OK;
  }

  zx_status_t UsbHciResetDevice(uint32_t hub_address, uint32_t device_id) {
    if (device_id == kDeviceId) {
      device_reset_ = true;
    }
    return ZX_OK;
  }

  size_t UsbHciGetMaxTransferSize(uint32_t device_id, uint8_t ep_address) {
    return ((device_id == kDeviceId) && (ep_address == kTransferSizeEndpoint)) ? kMaxTransferSize
                                                                               : 0;
  }

  zx_status_t UsbHciCancelAll(uint32_t device_id, uint8_t ep_address) {
    auto requests = pending_requests();
    requests.CompleteAll(ZX_ERR_CANCELED, 0);
    return ZX_OK;
  }

  void UsbHciSetBusInterface(const usb_bus_interface_protocol_t* bus_intf) {}

  size_t UsbHciGetMaxDeviceCount() { return 0; }

  size_t UsbHciGetRequestSize() {
    return usb::BorrowedRequest<void>::RequestSize(sizeof(usb_request_t));
  }

  void UsbHciRequestQueue(usb_request_t* usb_request_,
                          const usb_request_complete_callback_t* complete_cb_) {
    usb::BorrowedRequest<void> request(usb_request_, *complete_cb_, sizeof(usb_request_t));
    if (should_return_empty_) {
      request.Complete(ZX_OK, 0);
      return;
    }
    if ((request.request()->header.ep_address == 0) && !custom_control_) {
      if ((request.request()->setup.bm_request_type ==
           (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE)) &&
          (request.request()->setup.b_request == USB_REQ_GET_DESCRIPTOR)) {
        uint8_t type = static_cast<uint8_t>(request.request()->setup.w_value >> 8);
        uint8_t index = static_cast<uint8_t>(request.request()->setup.w_value);
        switch (type) {
          case USB_DT_DEVICE: {
            usb_device_descriptor_t* descriptor;
            request.Mmap(reinterpret_cast<void**>(&descriptor));
            descriptor->b_num_configurations = 2;
            descriptor->id_vendor = kVendorId;
            descriptor->id_product = kProductId;
            descriptor->b_device_class = kDeviceClass;
            descriptor->b_device_sub_class = kDeviceSubclass;
            descriptor->b_device_protocol = kDeviceProtocol;
            request.Complete(ZX_OK, sizeof(*descriptor));
          }
            return;
          case USB_DT_CONFIG: {
            usb_configuration_descriptor_t* descriptor;
            request.Mmap(reinterpret_cast<void**>(&descriptor));
            descriptor->w_total_length = sizeof(*descriptor);
            descriptor->b_configuration_value = static_cast<uint8_t>(index + 1);
            request.Complete(ZX_OK, sizeof(*descriptor));
          }
            return;
          case USB_DT_STRING: {
            if (index == 0) {
              // Fetch language table
              usb_langid_desc_t* languages;
              request.Mmap(reinterpret_cast<void**>(&languages));
              languages->b_length = 2 + (2 * 2);
              languages->w_lang_ids[0] = MakeConstant<uint16_t, 2>("EN");
              languages->w_lang_ids[1] = MakeConstant<uint16_t, 2>("ES");
              request.Complete(ZX_OK, languages->b_length);
              return;
            }
            index--;
            uint16_t lang = request.request()->setup.w_index;
            switch (lang) {
              case MakeConstant<uint16_t, 2>("EN"):
                lang = 0;
                break;
              case MakeConstant<uint16_t, 2>("ES"):
                lang = 1;
                break;
            }
            if ((index < 2) && (lang < 2)) {
              usb_string_desc_t* descriptor;
              request.Mmap(reinterpret_cast<void**>(&descriptor));
              descriptor->b_length = static_cast<uint8_t>(
                  2 + (2 * std::char_traits<char16_t>::length(kStringDescriptors[index][lang])));
              memcpy(descriptor->code_points, kStringDescriptors[index][lang],
                     descriptor->b_length - 2);
              request.Complete(ZX_OK, descriptor->b_length);
              return;
            }
          }
        }
      }
      if ((request.request()->setup.bm_request_type ==
           (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE)) &&
          (request.request()->setup.b_request == USB_REQ_SET_CONFIGURATION)) {
        selected_configuration_ = static_cast<uint8_t>(request.request()->setup.w_value);
        request.Complete(ZX_OK, 0);
        return;
      }
      request.Complete(ZX_ERR_INVALID_ARGS, 0);
      return;
    }
    pending_requests_.push(std::move(request));
  }

  zx_status_t UsbHciEnableEndpoint(uint32_t device_id, const usb_endpoint_descriptor_t* ep_desc,
                                   const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable) {
    if (!enable_endpoint_hook_) {
      return ZX_ERR_BAD_STATE;
    }
    return enable_endpoint_hook_(device_id, ep_desc, ss_com_desc, enable);
  }

  void SetEmptyState(bool should_return_empty) { should_return_empty_ = should_return_empty; }

  const usb_hci_protocol_t* proto() { return &proto_; }

  uint8_t configuration() { return selected_configuration_; }

  usb::BorrowedRequestQueue<void> pending_requests() { return std::move(pending_requests_); }

  void set_custom_control_handling(bool enabled) { custom_control_ = enabled; }

  void set_enable_endpoint_hook(
      fit::function<zx_status_t(uint32_t device_id, const usb_endpoint_descriptor_t* ep_desc,
                                const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable)>
          hook) {
    enable_endpoint_hook_ = std::move(hook);
  }

  uint8_t reset_endpoint() { return reset_endpoint_; }

  bool device_reset() { return device_reset_; }

 private:
  bool should_return_empty_ = false;
  bool device_reset_ = false;
  bool custom_control_ = false;
  uint8_t selected_configuration_ = 0;
  usb_hci_protocol_t proto_;
  uint8_t reset_endpoint_ = 0;
  fit::function<zx_status_t(uint32_t device_id, const usb_endpoint_descriptor_t* ep_desc,
                            const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable)>
      enable_endpoint_hook_;
  usb::BorrowedRequestQueue<void> pending_requests_;
};

class FakeTimer : public UsbWaiterInterface {
 public:
  zx_status_t Wait(sync_completion_t* completion, zx_duration_t duration) override {
    return timeout_handler_(completion, duration);
  }

  void set_timeout_handler(fit::function<zx_status_t(sync_completion_t*, zx_duration_t)> handler) {
    timeout_handler_ = std::move(handler);
  }

 private:
  fit::function<zx_status_t(sync_completion_t*, zx_duration_t)> timeout_handler_;
};

class DeviceTest : public zxtest::Test {
 public:
  DeviceTest() {
    timer_ = fbl::MakeRefCounted<FakeTimer>();
    timer_->set_timeout_handler([=](sync_completion_t* completion, zx_duration_t duration) {
      return sync_completion_wait(completion, duration);
    });
  }

  auto& get_fidl() {
    if (!fidl_.has_value()) {
      fidl_.emplace(std::move(ddk_.FidlClient()));
    }
    return fidl_.value();
  }

  auto& get_device() { return *device_; }

  void SetUp() override {
    auto device = fbl::MakeRefCounted<UsbDevice>(fake_ddk::kFakeParent,
                                                 ddk::UsbHciProtocolClient(hci_.proto()), kDeviceId,
                                                 kHubId, kDeviceSpeed, timer_);
    ASSERT_OK(device->Init());
    device_ = device.get();
  }

  void TearDown() override {
    device_async_remove(fake_ddk::kFakeDevice);
    ddk_.WaitUntilRemove();
    ASSERT_TRUE(ddk_.get_remove_called());
    device_->DdkRelease();
  }

  void CancelAll() { ASSERT_OK(device_->UsbCancelAll(1)); }

  size_t get_parent_request_size() { return device_->UsbGetRequestSize(); }

  void RequestQueue(usb_request_t* request, const usb_request_complete_callback_t* completion) {
    device_->UsbRequestQueue(request, completion);
  }

  ddk::UsbProtocolClient get_usb_protocol() {
    usb_protocol_t usb;
    device_->DdkGetProtocol(ZX_PROTOCOL_USB, &usb);
    return ddk::UsbProtocolClient(&usb);
  }

  ddk::UsbBusProtocolClient get_usb_bus_protocol() {
    usb_bus_protocol_t usb;
    device_->DdkGetProtocol(ZX_PROTOCOL_USB_BUS, &usb);
    return ddk::UsbBusProtocolClient(&usb);
  }

  void set_custom_control_handling(bool enabled) { hci_.set_custom_control_handling(enabled); }

  usb::BorrowedRequestQueue<void> get_pending_requests() { return hci_.pending_requests(); }

  uint8_t get_configuration() { return hci_.configuration(); }

  void set_enable_endpoint_hook(
      fit::function<zx_status_t(uint32_t device_id, const usb_endpoint_descriptor_t* ep_desc,
                                const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable)>
          hook) {
    hci_.set_enable_endpoint_hook(std::move(hook));
  }

  void set_timeout_handler(fit::function<zx_status_t(sync_completion_t*, zx_duration_t)> handler) {
    timer_->set_timeout_handler(std::move(handler));
  }

  bool get_device_reset() { return hci_.device_reset(); }

  uint8_t get_reset_endpoint() { return hci_.reset_endpoint(); }

  void SetEmptyState(bool should_return_empty) { hci_.SetEmptyState(should_return_empty); }

 private:
  fbl::RefPtr<FakeTimer> timer_;
  std::optional<fidl::WireSyncClient<fuchsia_hardware_usb_device::Device>> fidl_;
  FakeHci hci_;
  Binder ddk_;
  // UsbDevice context pointer owned by us through FakeDDK
  // This is freed by calling DdkRelease in TearDown.
  UsbDevice* device_;
};

// CancelAll-specific test

TEST_F(DeviceTest, CancelAllCancelsAllRequestsThenReturns) {
  using Request = usb::CallbackRequest<sizeof(max_align_t)>;
  std::atomic<size_t> completed = 0;
  for (size_t i = 0; i < 500; i++) {
    std::optional<Request> request;
    Request::Alloc(&request, 0, 1, get_parent_request_size(),
                   [&](Request request) { completed++; });
    request->Queue(*this);
  }
  CancelAll();
  ASSERT_EQ(completed.load(), 500);
}

// USB protocol tests

TEST_F(DeviceTest, ControlOut) {
  auto usb = get_usb_protocol();
  uint8_t data[5];
  for (size_t i = 0; i < 5; i++) {
    data[i] = static_cast<uint8_t>(i);
  }
  set_custom_control_handling(true);
  set_timeout_handler([&](sync_completion_t* completion, zx_duration_t duration) {
    EXPECT_EQ(duration, 9001);
    auto requests = get_pending_requests();
    auto request = requests.pop();
    EXPECT_EQ(request->request()->header.length, sizeof(data));
    void* mapped_data;
    request->Mmap(&mapped_data);
    EXPECT_EQ(0, memcmp(data, data, sizeof(data)));
    EXPECT_EQ(request->request()->setup.bm_request_type, 5);
    EXPECT_EQ(request->request()->setup.b_request, 97);
    EXPECT_EQ(request->request()->setup.w_value, 8);
    EXPECT_EQ(request->request()->setup.w_index, 12);

    request->Complete(ZX_OK, sizeof(data));
    return sync_completion_wait(completion, ZX_TIME_INFINITE);
  });
  ASSERT_OK(usb.ControlOut(5, 97, 8, 12, 9001, data, sizeof(data)));
}

TEST_F(DeviceTest, ControlIn) {
  auto usb = get_usb_protocol();
  uint8_t data[5];
  for (size_t i = 0; i < 5; i++) {
    data[i] = static_cast<uint8_t>(i);
  }
  set_custom_control_handling(true);
  set_timeout_handler([&](sync_completion_t* completion, zx_duration_t duration) {
    EXPECT_EQ(duration, 9001);
    auto requests = get_pending_requests();
    auto request = requests.pop();
    EXPECT_EQ(request->request()->header.length, sizeof(data));
    void* mapped_data;
    request->Mmap(&mapped_data);
    memcpy(mapped_data, data, sizeof(data));
    EXPECT_EQ(request->request()->setup.bm_request_type, 5 | USB_DIR_IN);
    EXPECT_EQ(request->request()->setup.b_request, 97);
    EXPECT_EQ(request->request()->setup.w_value, 8);
    EXPECT_EQ(request->request()->setup.w_index, 12);

    request->Complete(ZX_OK, sizeof(data));
    return sync_completion_wait(completion, ZX_TIME_INFINITE);
  });
  uint8_t buffer[5];
  size_t actual;
  ASSERT_OK(usb.ControlIn(5 | USB_DIR_IN, 97, 8, 12, 9001, buffer, sizeof(buffer), &actual));
  ASSERT_EQ(0, memcmp(buffer, data, sizeof(buffer)));
}

TEST_F(DeviceTest, RequestQueue) {
  auto usb = get_usb_protocol();
  using Request = usb::CallbackRequest<sizeof(max_align_t)>;
  std::optional<Request> request;
  sync_completion_t completion;
  usb_request_t* request_ptr;
  Request::Alloc(&request, 0, 1, get_parent_request_size(), [&](Request owned_request) {
    ASSERT_EQ(owned_request.request(), request_ptr);
    sync_completion_signal(&completion);
  });
  request_ptr = request->request();
  request->Queue(usb);
  auto requests = get_pending_requests();
  auto usb_request = requests.pop();
  usb_request->Complete(ZX_OK, 0);
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
}

TEST_F(DeviceTest, GetSpeed) {
  auto usb = get_usb_protocol();
  ASSERT_EQ(usb.GetSpeed(), kDeviceSpeed);
}

TEST_F(DeviceTest, SetInterface) {
  auto usb = get_usb_protocol();
  set_custom_control_handling(true);
  set_timeout_handler([&](sync_completion_t* completion, zx_duration_t duration) {
    EXPECT_EQ(duration, ZX_TIME_INFINITE);
    auto requests = get_pending_requests();
    auto request = requests.pop();
    EXPECT_EQ(request->request()->header.ep_address, 0);
    EXPECT_EQ(request->request()->setup.bm_request_type,
              USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE);
    EXPECT_EQ(request->request()->setup.b_request, USB_REQ_SET_INTERFACE);
    EXPECT_EQ(request->request()->setup.w_value, 5);
    EXPECT_EQ(request->request()->setup.w_index, 98);
    request->Complete(ZX_OK, 0);
    return sync_completion_wait(completion, ZX_TIME_INFINITE);
  });
  usb.SetInterface(98, 5);
}

TEST_F(DeviceTest, GetConfiguration) {
  auto usb = get_usb_protocol();
  ASSERT_EQ(usb.GetConfiguration(), 1);
}

TEST_F(DeviceTest, SetConfiguration) {
  auto usb = get_usb_protocol();
  ASSERT_OK(usb.SetConfiguration(2));
  ASSERT_EQ(get_configuration(), 2);
}

TEST_F(DeviceTest, EnableEndpoint) {
  auto usb = get_usb_protocol();
  usb_endpoint_descriptor_t epdesc;
  usb_ss_ep_comp_descriptor_t ss;
  set_enable_endpoint_hook([&](uint32_t device_id, const usb_endpoint_descriptor_t* ep_desc,
                               const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable) {
    EXPECT_EQ(device_id, kDeviceId);
    EXPECT_EQ(ep_desc, &epdesc);
    EXPECT_EQ(ss_com_desc, &ss);
    EXPECT_TRUE(enable);
    return ZX_OK;
  });
  ASSERT_OK(usb.EnableEndpoint(&epdesc, &ss, true));
}

TEST_F(DeviceTest, ResetEndpoint) {
  auto usb = get_usb_protocol();
  ASSERT_OK(usb.ResetEndpoint(97));
  ASSERT_EQ(get_reset_endpoint(), 97);
}

TEST_F(DeviceTest, ResetDevice) {
  auto usb = get_usb_protocol();
  ASSERT_OK(usb.ResetDevice());
  ASSERT_TRUE(get_device_reset());
}

TEST_F(DeviceTest, GetMaxTransferSize) {
  auto usb = get_usb_protocol();
  ASSERT_EQ(usb.GetMaxTransferSize(kTransferSizeEndpoint), kMaxTransferSize);
}

TEST_F(DeviceTest, GetDeviceId) {
  auto usb = get_usb_protocol();
  ASSERT_EQ(usb.GetDeviceId(), kDeviceId);
}

TEST_F(DeviceTest, GetDeviceDescriptor) {
  auto usb = get_usb_protocol();
  usb_device_descriptor_t descriptor;
  usb.GetDeviceDescriptor(&descriptor);
  ASSERT_EQ(descriptor.id_vendor, kVendorId);
  ASSERT_EQ(descriptor.id_product, kProductId);
  ASSERT_EQ(descriptor.b_device_class, kDeviceClass);
  ASSERT_EQ(descriptor.b_device_sub_class, kDeviceSubclass);
  ASSERT_EQ(descriptor.b_device_protocol, kDeviceProtocol);
}

TEST_F(DeviceTest, GetConfigurationDescriptorLength) {
  auto usb = get_usb_protocol();
  size_t length;
  ASSERT_OK(usb.GetConfigurationDescriptorLength(1, &length));
  ASSERT_EQ(length, sizeof(usb_configuration_descriptor_t));
}

TEST_F(DeviceTest, GetConfigurationDescriptor) {
  auto usb = get_usb_protocol();
  usb_configuration_descriptor_t descriptor;
  size_t actual;
  ASSERT_OK(usb.GetConfigurationDescriptor(1, reinterpret_cast<uint8_t*>(&descriptor),
                                           sizeof(descriptor), &actual));
  ASSERT_EQ(actual, sizeof(descriptor));
  ASSERT_EQ(descriptor.b_configuration_value, 1);
  ASSERT_EQ(descriptor.w_total_length, sizeof(descriptor));
}

TEST_F(DeviceTest, GetDescriptorsLength) {
  auto usb = get_usb_protocol();
  ASSERT_EQ(usb.GetDescriptorsLength(), sizeof(usb_configuration_descriptor_t));
}

TEST_F(DeviceTest, GetDescriptors) {
  auto usb = get_usb_protocol();
  usb_configuration_descriptor_t descriptor;
  size_t actual;
  usb.GetDescriptors(reinterpret_cast<uint8_t*>(&descriptor), sizeof(descriptor), &actual);
  ASSERT_EQ(actual, sizeof(descriptor));
  ASSERT_EQ(descriptor.b_configuration_value, 1);
  ASSERT_EQ(descriptor.w_total_length, sizeof(descriptor));
}

TEST_F(DeviceTest, GetCurrentFrame) {
  auto usb = get_usb_protocol();
  ASSERT_EQ(usb.GetCurrentFrame(), kCurrentFrame);
}

TEST_F(DeviceTest, GetRequestSize) {
  auto usb = get_usb_protocol();
  ASSERT_EQ(usb.GetRequestSize(), kRequestSize);
}

TEST_F(DeviceTest, FidlGetSpeed) {
  auto& fidl = get_fidl();
  auto result = fidl.GetDeviceSpeed();
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->speed, kDeviceSpeed);
}

TEST_F(DeviceTest, FidlGetDescriptor) {
  auto& fidl = get_fidl();
  auto result = fidl.GetDeviceDescriptor();
  usb_device_descriptor_t* descriptor =
      reinterpret_cast<usb_device_descriptor_t*>(result->desc.data());
  ASSERT_EQ(descriptor->id_vendor, kVendorId);
  ASSERT_EQ(descriptor->id_product, kProductId);
  ASSERT_EQ(descriptor->b_device_class, kDeviceClass);
  ASSERT_EQ(descriptor->b_device_sub_class, kDeviceSubclass);
  ASSERT_EQ(descriptor->b_device_protocol, kDeviceProtocol);
}

TEST_F(DeviceTest, FidlGetConfigurationDescriptorSize) {
  auto& fidl = get_fidl();
  auto result = fidl.GetConfigurationDescriptorSize(1);
  ASSERT_TRUE(result.ok());
  ASSERT_OK(result->s);
  ASSERT_EQ(result->size, sizeof(usb_configuration_descriptor_t));
}

TEST_F(DeviceTest, FidlGetConfigurationDescriptor) {
  auto& fidl = get_fidl();
  const usb_configuration_descriptor_t* descriptor;
  auto result = fidl.GetConfigurationDescriptor(1);
  ASSERT_TRUE(result.ok());
  ASSERT_OK(result->s);
  ASSERT_EQ(result->desc.count(), sizeof(*descriptor));
  descriptor = reinterpret_cast<const usb_configuration_descriptor_t*>(result->desc.data());
  ASSERT_EQ(descriptor->b_configuration_value, 1);
  ASSERT_EQ(descriptor->w_total_length, sizeof(*descriptor));
}

TEST_F(DeviceTest, FidlGetStringDescriptor_Empty) {
  auto& fidl = get_fidl();
  char golden[128];
  size_t dest_len = 128;
  utf16_to_utf8(reinterpret_cast<const uint16_t*>(kStringDescriptors[0][0]), 7,
                reinterpret_cast<uint8_t*>(golden), &dest_len);
  SetEmptyState(true);
  auto result = fidl.GetStringDescriptor(1, MakeConstant<uint16_t, 2>("EN"));
  ASSERT_TRUE(result->desc.empty());
  ASSERT_EQ(result->s, ZX_ERR_INTERNAL);
}

TEST_F(DeviceTest, FidlGetStringDescriptor) {
  auto& fidl = get_fidl();
  char golden[128];
  {
    size_t dest_len = 128;
    utf16_to_utf8(reinterpret_cast<const uint16_t*>(kStringDescriptors[0][0]), 7,
                  reinterpret_cast<uint8_t*>(golden), &dest_len);
    auto expected = MakeConstant<uint16_t, 2>("EN");
    auto result = fidl.GetStringDescriptor(1, MakeConstant<uint16_t, 2>("EN"));
    ASSERT_TRUE(result.ok());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->actual_lang_id, expected);
    ASSERT_EQ(result->desc.size(), dest_len);  // this is way off for some reason
    ASSERT_EQ(memcmp(result->desc.data(), golden, dest_len), 0);
  }

  {
    size_t dest_len = 128;
    utf16_to_utf8(reinterpret_cast<const uint16_t*>(kStringDescriptors[0][1]), 6,
                  reinterpret_cast<uint8_t*>(golden), &dest_len);
    auto expected = MakeConstant<uint16_t, 2>("ES");
    auto result = fidl.GetStringDescriptor(1, MakeConstant<uint16_t, 2>("ES"));
    ASSERT_TRUE(result.ok());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->actual_lang_id, expected);
    ASSERT_EQ(result->desc.size(), dest_len);
    ASSERT_EQ(memcmp(result->desc.data(), golden, dest_len), 0);
  }

  {
    size_t dest_len = 128;
    utf16_to_utf8(reinterpret_cast<const uint16_t*>(kStringDescriptors[1][0]), 6,
                  reinterpret_cast<uint8_t*>(golden), &dest_len);
    auto expected = MakeConstant<uint16_t, 2>("EN");
    auto result = fidl.GetStringDescriptor(2, MakeConstant<uint16_t, 2>("EN"));
    ASSERT_TRUE(result.ok());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->actual_lang_id, expected);
    ASSERT_EQ(result->desc.size(), dest_len);
    ASSERT_EQ(memcmp(result->desc.data(), golden, dest_len), 0);
  }

  {
    size_t dest_len = 128;
    utf16_to_utf8(reinterpret_cast<const uint16_t*>(kStringDescriptors[1][1]), 11,
                  reinterpret_cast<uint8_t*>(golden), &dest_len);
    auto expected = MakeConstant<uint16_t, 2>("ES");
    auto result = fidl.GetStringDescriptor(2, MakeConstant<uint16_t, 2>("ES"));
    ASSERT_TRUE(result.ok());
    ASSERT_OK(result->s);
    ASSERT_EQ(result->actual_lang_id, expected);
    ASSERT_EQ(result->desc.size(), dest_len);
    ASSERT_EQ(memcmp(result->desc.data(), golden, dest_len), 0);
  }
}

TEST_F(DeviceTest, UsbGetStringDescriptor_BufferTooSmall) {
  auto& device = get_device();
  uint16_t lang_id[2];
  uint8_t desc[128];
  size_t actual;

  // The value here is intentionally chosen to be undersized.
  size_t small = 3;

  zx_status_t status = device.UsbGetStringDescriptor(
      1, 1, lang_id, reinterpret_cast<uint8_t*>(&desc), small, &actual);

  EXPECT_EQ(status, ZX_ERR_BUFFER_TOO_SMALL);
  EXPECT_GT(actual, small);
}

TEST_F(DeviceTest, FidlSetInterface) {
  auto& fidl = get_fidl();
  set_custom_control_handling(true);
  set_timeout_handler([&](sync_completion_t* completion, zx_duration_t duration) {
    EXPECT_EQ(duration, ZX_TIME_INFINITE);
    auto requests = get_pending_requests();
    auto request = requests.pop();
    EXPECT_EQ(request->request()->header.ep_address, 0);
    EXPECT_EQ(request->request()->setup.bm_request_type,
              USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE);
    EXPECT_EQ(request->request()->setup.b_request, USB_REQ_SET_INTERFACE);
    EXPECT_EQ(request->request()->setup.w_value, 5);
    EXPECT_EQ(request->request()->setup.w_index, 98);
    request->Complete(ZX_OK, 0);
    return sync_completion_wait(completion, ZX_TIME_INFINITE);
  });
  auto result = fidl.SetInterface(98, 5);
  ASSERT_TRUE(result.ok());
  ASSERT_OK(result->s);
}

TEST_F(DeviceTest, FidlGetDeviceId) {
  auto& fidl = get_fidl();
  auto result = fidl.GetDeviceId();
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->device_id, kDeviceId);
}

TEST_F(DeviceTest, FidlGetHubDeviceId) {
  auto& fidl = get_fidl();
  auto result = fidl.GetHubDeviceId();
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->hub_device_id, kHubId);
}

TEST_F(DeviceTest, FidlGetConfiguration) {
  auto& fidl = get_fidl();
  auto result = fidl.GetConfiguration();
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->configuration, 1);
}

TEST_F(DeviceTest, FidlSetConfiguration) {
  auto& fidl = get_fidl();
  auto result = fidl.SetConfiguration(2);
  ASSERT_TRUE(result.ok());
  ASSERT_OK(result->s);
  ASSERT_EQ(get_configuration(), 2);
}

class EvilFakeHci : public ddk::UsbHciProtocol<EvilFakeHci> {
  // A fake HCI that pretends to be a device that does dodgy things with
  // configuration descriptors: namely, changing the size they claim to be
  // depending on how many requests for config descriptors have been made
  // previously.
 public:
  EvilFakeHci(uint16_t initial_config_length, uint16_t subsequent_config_length) {
    initial_config_length_ = initial_config_length;
    subsequent_config_length_ = subsequent_config_length;
    proto_.ops = &usb_hci_protocol_ops_;
    proto_.ctx = this;
  }
  uint64_t UsbHciGetCurrentFrame() { return kCurrentFrame; }

  zx_status_t UsbHciConfigureHub(uint32_t device_id, usb_speed_t speed,
                                 const usb_hub_descriptor_t* desc, bool multi_tt) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t UsbHciHubDeviceAdded(uint32_t device_id, uint32_t port, usb_speed_t speed) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t UsbHciHubDeviceRemoved(uint32_t device_id, uint32_t port) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t UsbHciHubDeviceReset(uint32_t device_id, uint32_t port) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t UsbHciResetEndpoint(uint32_t device_id, uint8_t ep_address) { return ZX_OK; }

  zx_status_t UsbHciResetDevice(uint32_t hub_address, uint32_t device_id) { return ZX_OK; }

  size_t UsbHciGetMaxTransferSize(uint32_t device_id, uint8_t ep_address) {
    return ((device_id == kDeviceId) && (ep_address == kTransferSizeEndpoint)) ? kMaxTransferSize
                                                                               : 0;
  }

  zx_status_t UsbHciCancelAll(uint32_t device_id, uint8_t ep_address) {
    auto requests = pending_requests();
    requests.CompleteAll(ZX_ERR_CANCELED, 0);
    return ZX_OK;
  }

  void UsbHciSetBusInterface(const usb_bus_interface_protocol_t* bus_intf) {}

  size_t UsbHciGetMaxDeviceCount() { return 0; }

  size_t UsbHciGetRequestSize() {
    return usb::BorrowedRequest<void>::RequestSize(sizeof(usb_request_t));
  }

  void UsbHciRequestQueue(usb_request_t* usb_request_,
                          const usb_request_complete_callback_t* complete_cb_) {
    usb::BorrowedRequest<void> request(usb_request_, *complete_cb_, sizeof(usb_request_t));
    EXPECT_EQ(request.request()->header.ep_address, 0);
    EXPECT_EQ(request.request()->setup.bm_request_type,
              USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE);
    EXPECT_EQ(request.request()->setup.b_request, USB_REQ_GET_DESCRIPTOR);

    if (request.request()->header.ep_address == 0) {
      if ((request.request()->setup.bm_request_type ==
           (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE)) &&
          (request.request()->setup.b_request == USB_REQ_GET_DESCRIPTOR)) {
        uint8_t type = static_cast<uint8_t>(request.request()->setup.w_value >> 8);
        uint8_t index = static_cast<uint8_t>(request.request()->setup.w_value);
        switch (type) {
          case USB_DT_DEVICE: {
            usb_device_descriptor_t* descriptor;
            request.Mmap(reinterpret_cast<void**>(&descriptor));
            descriptor->b_num_configurations = 2;
            descriptor->id_vendor = kVendorId;
            descriptor->id_product = kProductId;
            descriptor->b_device_class = kDeviceClass;
            descriptor->b_device_sub_class = kDeviceSubclass;
            descriptor->b_device_protocol = kDeviceProtocol;
            request.Complete(ZX_OK, sizeof(*descriptor));
          }
            return;
          case USB_DT_CONFIG: {
            usb_configuration_descriptor_t* descriptor;
            request.Mmap(reinterpret_cast<void**>(&descriptor));
            // Use the config descriptor lengths described in the constructor
            // arguments.
            descriptor->w_total_length =
                (config_descriptor_request_count_ % 2 == 0 ? initial_config_length_
                                                           : subsequent_config_length_);
            config_descriptor_request_count_++;
            descriptor->b_configuration_value = static_cast<uint8_t>(index + 1);
            request.Complete(ZX_OK, sizeof(*descriptor));
          }
            return;
        }
      }

      // The host should not send us any requests (like attempting to set a configuration)
      // after we do questionable things with wTotalLength.
      request.Complete(ZX_ERR_INVALID_ARGS, 0);
      return;
    }
    pending_requests_.push(std::move(request));
  }

  zx_status_t UsbHciEnableEndpoint(uint32_t device_id, const usb_endpoint_descriptor_t* ep_desc,
                                   const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable) {
    return ZX_ERR_BAD_STATE;
  }

  const usb_hci_protocol_t* proto() { return &proto_; }

  usb::BorrowedRequestQueue<void> pending_requests() { return std::move(pending_requests_); }

 private:
  int config_descriptor_request_count_ = 0;
  uint16_t initial_config_length_;
  uint16_t subsequent_config_length_;
  usb_hci_protocol_t proto_;
  fit::function<zx_status_t(uint32_t device_id, const usb_endpoint_descriptor_t* ep_desc,
                            const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable)>
      enable_endpoint_hook_;
  usb::BorrowedRequestQueue<void> pending_requests_;
};

TEST(DeviceTest, GetConfigurationDescriptorTooShortRejected) {
  // We expect this device to fail to initialize because wTotalLength is too
  // short -- 1 byte is shorter than the minimal config descriptor length, so
  // such a response is invalid.
  EvilFakeHci hci(1, 1);
  fbl::RefPtr<FakeTimer> timer = fbl::MakeRefCounted<FakeTimer>();
  timer->set_timeout_handler([=](sync_completion_t* completion, zx_duration_t duration) {
    return sync_completion_wait(completion, duration);
  });

  auto device =
      fbl::MakeRefCounted<UsbDevice>(fake_ddk::kFakeParent, ddk::UsbHciProtocolClient(hci.proto()),
                                     kDeviceId, kHubId, kDeviceSpeed, timer);
  auto result = device->Init();
  ASSERT_EQ(result, ZX_ERR_IO);
}

TEST(DeviceTest, GetConfigurationDescriptorDifferentSizesAreRejected) {
  // We expect this device to fail to initialize because when we request its
  // configuration descriptors, the wTotalSize value we get back changes between
  // the first (size-fetching) request and second (full descriptor-fetching) request.
  EvilFakeHci hci(sizeof(usb_configuration_descriptor_t), 65535);
  fbl::RefPtr<FakeTimer> timer = fbl::MakeRefCounted<FakeTimer>();
  timer->set_timeout_handler([=](sync_completion_t* completion, zx_duration_t duration) {
    return sync_completion_wait(completion, duration);
  });

  auto device =
      fbl::MakeRefCounted<UsbDevice>(fake_ddk::kFakeParent, ddk::UsbHciProtocolClient(hci.proto()),
                                     kDeviceId, kHubId, kDeviceSpeed, timer);
  auto result = device->Init();
  ASSERT_EQ(result, ZX_ERR_IO);
}

}  // namespace usb_bus
