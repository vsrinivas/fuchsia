// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <usb/usb.h>
#include <zxtest/zxtest.h>

#include "lib/fit/defer.h"

namespace {

constexpr usb_descriptor_header_t kTestDescriptorHeader = {
    .bLength = sizeof(usb_descriptor_header_t),
    .bDescriptorType = 0,
};

constexpr usb_interface_descriptor_t kTestUsbInterfaceDescriptor = {
    .b_length = sizeof(usb_interface_descriptor_t),
    .b_descriptor_type = USB_DT_INTERFACE,
    .b_interface_number = 0,
    .b_alternate_setting = 0,
    .b_num_endpoints = 2,
    .b_interface_class = 8,
    .b_interface_sub_class = 6,
    .b_interface_protocol = 80,
    .i_interface = 0,
};

constexpr usb_endpoint_descriptor_t kTestUsbEndpointDescriptor = {
    .b_length = sizeof(usb_endpoint_descriptor_t),
    .b_descriptor_type = USB_DT_ENDPOINT,
    .b_endpoint_address = 0x81,
    .bm_attributes = 2,
    .w_max_packet_size = 1024,
    .b_interval = 0,
};

constexpr usb_ss_ep_comp_descriptor_t kTestUsbSsEpCompDescriptor = {
    .b_length = sizeof(usb_ss_ep_comp_descriptor_t),
    .b_descriptor_type = USB_DT_SS_EP_COMPANION,
    .b_max_burst = 3,
    .bm_attributes = 0,
    .w_bytes_per_interval = 0,
};

class UsbLibTest : public zxtest::Test {
 public:
  void SetUp() override {
    proto_.ops = &ops_;
    proto_.ctx = this;
    ops_.get_descriptors_length = UsbGetDescriptorsLength;
    ops_.get_descriptors = UsbGetDescriptors;
  }

 protected:
  static void UsbGetDescriptors(void* ctx, uint8_t* out_descs_buffer, size_t descs_size,
                                size_t* out_descs_actual) {
    auto test = reinterpret_cast<UsbLibTest*>(ctx);
    size_t descriptors_length = test->GetDescriptorLength();
    if (descs_size < descriptors_length) {
      descriptors_length = descs_size;
    }
    memcpy(out_descs_buffer, test->GetDescriptors(), descriptors_length);
    *out_descs_actual = descriptors_length;
  }

  static size_t UsbGetDescriptorsLength(void* ctx) {
    auto test = reinterpret_cast<UsbLibTest*>(ctx);
    return test->GetDescriptorLength();
  }

  void SetDescriptors(void* descriptors) { descriptors_ = descriptors; }

  void* GetDescriptors() { return descriptors_; }

  void SetDescriptorLength(size_t descriptor_length) { descriptor_length_ = descriptor_length; }

  size_t GetDescriptorLength() { return descriptor_length_; }

  usb_protocol_t* GetUsbProto() { return &proto_; }

  usb_protocol_t proto_{};
  usb_protocol_ops_t ops_{};
  void* descriptors_ = nullptr;
  size_t descriptor_length_ = 0;
};

TEST_F(UsbLibTest, TestUsbDescIterPeekNormal) {
  usb_desc_iter_t iter;
  SetDescriptors((void*)&kTestDescriptorHeader);
  SetDescriptorLength(sizeof(kTestDescriptorHeader));
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  auto desc = usb_desc_iter_peek(&iter);
  ASSERT_EQ(memcmp(desc, &kTestDescriptorHeader, sizeof(kTestDescriptorHeader)), 0);
  usb_desc_iter_release(&iter);
}

TEST_F(UsbLibTest, TestUsbDescPeekOverflow) {
  usb_desc_iter_t iter;
  usb_descriptor_header_t desc = kTestDescriptorHeader;
  // Length is invalid and longer than the actual length.
  desc.bLength++;
  SetDescriptors((void*)&desc);
  SetDescriptorLength(sizeof(desc));
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  ASSERT_EQ(nullptr, usb_desc_iter_peek(&iter));
  usb_desc_iter_release(&iter);
}

TEST_F(UsbLibTest, TestUsbDescIterPeekHeaderTooShort) {
  usb_desc_iter_t iter;
  SetDescriptors((void*)&kTestDescriptorHeader);
  SetDescriptorLength(sizeof(kTestDescriptorHeader) - 1);
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  ASSERT_EQ(nullptr, usb_desc_iter_peek(&iter));
  usb_desc_iter_release(&iter);
}

TEST_F(UsbLibTest, TestUsbDescClone) {
  usb_desc_iter_t src;
  SetDescriptors((void*)&kTestDescriptorHeader);
  SetDescriptorLength(sizeof(kTestDescriptorHeader));
  auto status = usb_desc_iter_init(GetUsbProto(), &src);
  ASSERT_OK(status);
  usb_desc_iter_t dest;
  ASSERT_OK(usb_desc_iter_clone(&src, &dest));
  // This should not affect dest.
  usb_desc_iter_release(&src);
  auto desc = usb_desc_iter_peek(&dest);
  ASSERT_EQ(memcmp(desc, &kTestDescriptorHeader, sizeof(kTestDescriptorHeader)), 0);
  ASSERT_TRUE(usb_desc_iter_advance(&dest));
  ASSERT_EQ(nullptr, usb_desc_iter_peek(&dest));
  usb_desc_iter_release(&dest);
}

TEST_F(UsbLibTest, TestUsbDescAdvanceReset) {
  usb_desc_iter_t iter;
  SetDescriptors((void*)&kTestDescriptorHeader);
  SetDescriptorLength(sizeof(kTestDescriptorHeader));
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  ASSERT_TRUE(usb_desc_iter_advance(&iter));
  ASSERT_FALSE(usb_desc_iter_advance(&iter));
  usb_desc_iter_reset(&iter);
  auto desc = usb_desc_iter_peek(&iter);
  ASSERT_TRUE(usb_desc_iter_advance(&iter));
  ASSERT_EQ(memcmp(desc, &kTestDescriptorHeader, sizeof(kTestDescriptorHeader)), 0);
  ASSERT_EQ(nullptr, usb_desc_iter_peek(&iter));
  usb_desc_iter_release(&iter);
}

TEST_F(UsbLibTest, TestUsbDescGetStructureNormal) {
  usb_desc_iter_t iter;
  SetDescriptors((void*)&kTestUsbInterfaceDescriptor);
  SetDescriptorLength(sizeof(kTestUsbInterfaceDescriptor));
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  auto desc = usb_desc_iter_get_structure(&iter, sizeof(kTestUsbInterfaceDescriptor));
  ASSERT_EQ(memcmp(desc, &kTestUsbInterfaceDescriptor, sizeof(kTestUsbInterfaceDescriptor)), 0);
  ASSERT_TRUE(usb_desc_iter_advance(&iter));
  ASSERT_EQ(nullptr, usb_desc_iter_get_structure(&iter, sizeof(kTestUsbInterfaceDescriptor)));
  usb_desc_iter_release(&iter);
}

TEST_F(UsbLibTest, TestUsbDescGetStructureOverflow) {
  usb_desc_iter_t iter;
  usb_interface_descriptor_t desc = kTestUsbInterfaceDescriptor;
  SetDescriptors((void*)&desc);
  SetDescriptorLength(sizeof(desc) - 1);
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  ASSERT_EQ(nullptr, usb_desc_iter_get_structure(&iter, sizeof(kTestUsbInterfaceDescriptor)));
  usb_desc_iter_release(&iter);
}

TEST_F(UsbLibTest, TestUsbDescIterNextInterface) {
  // Layout is | Intf | Ep | SsEp | Intf | Ep | SsEp |.
  size_t desc_length = (sizeof(kTestUsbInterfaceDescriptor) + sizeof(kTestUsbEndpointDescriptor) +
                        sizeof(kTestUsbSsEpCompDescriptor)) *
                       2;
  void* desc = malloc(desc_length);
  auto cleanup = fit::defer([desc]() { free(desc); });
  uint8_t* ptr = reinterpret_cast<uint8_t*>(desc);
  usb_desc_iter_t iter;
  for (size_t i = 0; i < 2; i++) {
    memcpy(ptr, &kTestUsbInterfaceDescriptor, sizeof(kTestUsbInterfaceDescriptor));
    ptr += sizeof(kTestUsbInterfaceDescriptor);
    memcpy(ptr, &kTestUsbEndpointDescriptor, sizeof(kTestUsbEndpointDescriptor));
    ptr += sizeof(kTestUsbEndpointDescriptor);
    memcpy(ptr, &kTestUsbSsEpCompDescriptor, sizeof(kTestUsbSsEpCompDescriptor));
    ptr += sizeof(kTestUsbSsEpCompDescriptor);
  }
  SetDescriptors(desc);
  SetDescriptorLength(desc_length);
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  auto iter_cleanup = fit::defer([&iter]() { usb_desc_iter_release(&iter); });
  for (size_t i = 0; i < 2; i++) {
    usb_interface_descriptor_t* interface = usb_desc_iter_next_interface(&iter, false);
    ASSERT_NE(nullptr, interface);
    ASSERT_EQ(memcmp(interface, &kTestUsbInterfaceDescriptor, sizeof(kTestUsbInterfaceDescriptor)),
              0);
  }
  ASSERT_EQ(nullptr, usb_desc_iter_next_interface(&iter, false));
}

TEST_F(UsbLibTest, TestUsbDescIterNextEndpoint) {
  // Layout is | Intf | Ep | Ep | Intf |.
  size_t desc_length =
      sizeof(kTestUsbInterfaceDescriptor) * 2 + sizeof(kTestUsbEndpointDescriptor) * 2;
  void* desc = malloc(desc_length);
  auto cleanup = fit::defer([desc]() { free(desc); });
  uint8_t* ptr = reinterpret_cast<uint8_t*>(desc);
  usb_desc_iter_t iter;
  memcpy(ptr, &kTestUsbInterfaceDescriptor, sizeof(kTestUsbInterfaceDescriptor));
  ptr += sizeof(kTestUsbInterfaceDescriptor);
  for (size_t i = 0; i < 2; i++) {
    memcpy(ptr, &kTestUsbEndpointDescriptor, sizeof(kTestUsbEndpointDescriptor));
    ptr += sizeof(kTestUsbEndpointDescriptor);
  }
  memcpy(ptr, &kTestUsbInterfaceDescriptor, sizeof(kTestUsbInterfaceDescriptor));
  ptr += sizeof(kTestUsbInterfaceDescriptor);
  SetDescriptors(desc);
  SetDescriptorLength(desc_length);
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  auto iter_cleanup = fit::defer([&iter]() { usb_desc_iter_release(&iter); });
  ASSERT_NE(nullptr, usb_desc_iter_next_interface(&iter, false));
  for (size_t i = 0; i < 2; i++) {
    usb_endpoint_descriptor_t* ep = usb_desc_iter_next_endpoint(&iter);
    ASSERT_NE(nullptr, ep);
    ASSERT_EQ(memcmp(ep, &kTestUsbEndpointDescriptor, sizeof(kTestUsbEndpointDescriptor)), 0);
  }
  ASSERT_EQ(nullptr, usb_desc_iter_next_endpoint(&iter));
}

TEST_F(UsbLibTest, TestUsbDescIterNextSsEpComp) {
  // Layout is | Intf | Ep | SsEp | SsEp | Intf |.
  size_t desc_length = sizeof(kTestUsbInterfaceDescriptor) * 2 +
                       sizeof(kTestUsbEndpointDescriptor) + sizeof(kTestUsbSsEpCompDescriptor) * 2;
  void* desc = malloc(desc_length);
  auto cleanup = fit::defer([desc]() { free(desc); });
  uint8_t* ptr = reinterpret_cast<uint8_t*>(desc);
  usb_desc_iter_t iter;
  memcpy(ptr, &kTestUsbInterfaceDescriptor, sizeof(kTestUsbInterfaceDescriptor));
  ptr += sizeof(kTestUsbInterfaceDescriptor);
  memcpy(ptr, &kTestUsbEndpointDescriptor, sizeof(kTestUsbEndpointDescriptor));
  ptr += sizeof(kTestUsbEndpointDescriptor);
  memcpy(ptr, &kTestUsbSsEpCompDescriptor, sizeof(kTestUsbSsEpCompDescriptor));
  ptr += sizeof(kTestUsbSsEpCompDescriptor);
  memcpy(ptr, &kTestUsbSsEpCompDescriptor, sizeof(kTestUsbSsEpCompDescriptor));
  ptr += sizeof(kTestUsbSsEpCompDescriptor);
  memcpy(ptr, &kTestUsbInterfaceDescriptor, sizeof(kTestUsbInterfaceDescriptor));
  ptr += sizeof(kTestUsbInterfaceDescriptor);
  SetDescriptors(desc);
  SetDescriptorLength(desc_length);
  ASSERT_OK(usb_desc_iter_init(GetUsbProto(), &iter));
  auto iter_cleanup = fit::defer([&iter]() { usb_desc_iter_release(&iter); });
  ASSERT_NE(nullptr, usb_desc_iter_next_interface(&iter, false));
  ASSERT_NE(nullptr, usb_desc_iter_next_endpoint(&iter));
  for (size_t i = 0; i < 2; i++) {
    usb_ss_ep_comp_descriptor_t* ss_ep = usb_desc_iter_next_ss_ep_comp(&iter);
    ASSERT_NE(nullptr, ss_ep);
    ASSERT_EQ(memcmp(ss_ep, &kTestUsbSsEpCompDescriptor, sizeof(kTestUsbSsEpCompDescriptor)), 0);
  }
  ASSERT_EQ(nullptr, usb_desc_iter_next_ss_ep_comp(&iter));
}

}  // namespace
