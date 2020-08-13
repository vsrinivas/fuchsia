// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <zxtest/zxtest.h>

#include "usb-cdc-ecm-lib.h"

namespace {

class UsbCdcEcmTest : public zxtest::Test {
 public:
  void SetUp() override {
    proto_.ops = &ops_;
    proto_.ctx = this;
    ops_.get_descriptors_length = UsbGetDescriptorsLength;
    ops_.get_descriptors = UsbGetDescriptors;
    ops_.control_in = UsbControlIn;
    iter = {};
  }

  void TearDown() override {
    if (iter.desc) {
      usb_desc_iter_release(&iter);
    }
  }

 protected:
  static void UsbGetDescriptors(void* ctx, void* out_descs_buffer, size_t descs_size,
                                size_t* out_descs_actual) {
    auto test = reinterpret_cast<UsbCdcEcmTest*>(ctx);
    size_t descriptors_length = test->GetDescriptorLength();
    if (descs_size < descriptors_length) {
      descriptors_length = descs_size;
    }
    memcpy(out_descs_buffer, test->GetDescriptors(), descriptors_length);
    *out_descs_actual = descriptors_length;
  }

  static size_t UsbGetDescriptorsLength(void* ctx) {
    auto test = reinterpret_cast<UsbCdcEcmTest*>(ctx);
    return test->GetDescriptorLength();
  }

  static zx_status_t UsbControlIn(void* ctx, uint8_t request_type, uint8_t request, uint16_t value,
                                  uint16_t index, int64_t timeout, void* out_read_buffer,
                                  size_t read_size, size_t* out_read_actual) {
    if (!(request_type & USB_DIR_IN && request == USB_REQ_GET_DESCRIPTOR)) {
      return ZX_ERR_INTERNAL;
    }
    const size_t expected_str_size = sizeof(usb_string_descriptor_t) + ETH_MAC_SIZE * 4;
    if (read_size < expected_str_size) {
      return ZX_ERR_INVALID_ARGS;
    }
    *out_read_actual = expected_str_size;
    usb_string_descriptor_t usb_str;
    usb_str.bLength = expected_str_size;
    usb_str.bDescriptorType = USB_DT_STRING;
    memcpy(out_read_buffer, &usb_str, sizeof(usb_string_descriptor_t));
    uint8_t* ptr = reinterpret_cast<uint8_t*>(out_read_buffer) + sizeof(usb_string_descriptor_t);
    for (size_t i = 0; i < ETH_MAC_SIZE; i++) {
      memcpy(ptr, "F\0F\0", 4);
      ptr += 4;
    }

    return ZX_OK;
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
  usb_desc_iter_t iter;
};

TEST_F(UsbCdcEcmTest, ParseUsbDescriptorTest) {
  std::vector<uint8_t> buffer;
  usb_interface_descriptor_t test_default_ifc = {
      .bLength = sizeof(usb_interface_descriptor_t),
      .bDescriptorType = USB_DT_INTERFACE,
      .bInterfaceNumber = 0,
      .bAlternateSetting = 0,
      .bNumEndpoints = 0,
      .bInterfaceClass = USB_CLASS_CDC,
      .bInterfaceSubClass = 0,
      .bInterfaceProtocol = 0,
      .iInterface = 0,
  };
  buffer.insert(buffer.end(), reinterpret_cast<uint8_t*>(&test_default_ifc),
                reinterpret_cast<uint8_t*>(&test_default_ifc) + sizeof(test_default_ifc));
  usb_interface_descriptor_t test_interrupt_ifc = {
      .bLength = sizeof(usb_interface_descriptor_t),
      .bDescriptorType = USB_DT_INTERFACE,
      .bInterfaceNumber = 0,
      .bAlternateSetting = 0,
      .bNumEndpoints = 1,
      .bInterfaceClass = USB_CLASS_CDC,
      .bInterfaceSubClass = 0,
      .bInterfaceProtocol = 0,
      .iInterface = 0,
  };
  buffer.insert(buffer.end(), reinterpret_cast<uint8_t*>(&test_interrupt_ifc),
                reinterpret_cast<uint8_t*>(&test_interrupt_ifc) + sizeof(test_interrupt_ifc));
  usb_endpoint_descriptor_t test_int_ep = {
      .bLength = sizeof(usb_endpoint_descriptor_t),
      .bDescriptorType = USB_DT_ENDPOINT,
      .bEndpointAddress = USB_ENDPOINT_IN,
      .bmAttributes = USB_ENDPOINT_INTERRUPT,
      .wMaxPacketSize = 0,
      .bInterval = 0,
  };
  buffer.insert(buffer.end(), reinterpret_cast<uint8_t*>(&test_int_ep),
                reinterpret_cast<uint8_t*>(&test_int_ep) + sizeof(test_int_ep));
  usb_interface_descriptor_t test_data_ifc = {
      .bLength = sizeof(usb_interface_descriptor_t),
      .bDescriptorType = USB_DT_INTERFACE,
      .bInterfaceNumber = 0,
      .bAlternateSetting = 0,
      .bNumEndpoints = 2,
      .bInterfaceClass = USB_CLASS_CDC,
      .bInterfaceSubClass = 0,
      .bInterfaceProtocol = 0,
      .iInterface = 0,
  };
  buffer.insert(buffer.end(), reinterpret_cast<uint8_t*>(&test_data_ifc),
                reinterpret_cast<uint8_t*>(&test_data_ifc) + sizeof(test_data_ifc));
  usb_endpoint_descriptor_t test_in_ep = {
      .bLength = sizeof(usb_endpoint_descriptor_t),
      .bDescriptorType = USB_DT_ENDPOINT,
      .bEndpointAddress = USB_ENDPOINT_IN,
      .bmAttributes = USB_ENDPOINT_BULK,
      .wMaxPacketSize = 0,
      .bInterval = 0,
  };
  buffer.insert(buffer.end(), reinterpret_cast<uint8_t*>(&test_in_ep),
                reinterpret_cast<uint8_t*>(&test_in_ep) + sizeof(test_in_ep));
  usb_endpoint_descriptor_t test_out_ep = {
      .bLength = sizeof(usb_endpoint_descriptor_t),
      .bDescriptorType = USB_DT_ENDPOINT,
      .bEndpointAddress = USB_ENDPOINT_OUT,
      .bmAttributes = USB_ENDPOINT_BULK,
      .wMaxPacketSize = 0,
      .bInterval = 0,
  };
  buffer.insert(buffer.end(), reinterpret_cast<uint8_t*>(&test_out_ep),
                reinterpret_cast<uint8_t*>(&test_out_ep) + sizeof(test_out_ep));
  usb_cs_header_interface_descriptor_t test_cdc_header_desc = {
      .bLength = sizeof(usb_cs_header_interface_descriptor_t),
      .bDescriptorType = USB_DT_CS_INTERFACE,
      .bDescriptorSubType = USB_CDC_DST_HEADER,
      .bcdCDC = CDC_SUPPORTED_VERSION,
  };
  buffer.insert(buffer.end(), reinterpret_cast<uint8_t*>(&test_cdc_header_desc),
                reinterpret_cast<uint8_t*>(&test_cdc_header_desc) + sizeof(test_cdc_header_desc));
  usb_cs_ethernet_interface_descriptor_t test_cdc_eth_ifc = {
      .bLength = sizeof(usb_cs_ethernet_interface_descriptor_t),
      .bDescriptorType = USB_DT_CS_INTERFACE,
      .bDescriptorSubType = USB_CDC_DST_ETHERNET,
      .iMACAddress = 1,
      .bmEthernetStatistics = 0,
      .wMaxSegmentSize = 1,
      .wNumberMCFilters = 0,
      .bNumberPowerFilters = 0,
  };
  buffer.insert(buffer.end(), reinterpret_cast<uint8_t*>(&test_cdc_eth_ifc),
                reinterpret_cast<uint8_t*>(&test_cdc_eth_ifc) + sizeof(test_cdc_eth_ifc));

  SetDescriptors(buffer.data());
  SetDescriptorLength(buffer.size());
  usb_endpoint_descriptor_t* int_ep = nullptr;
  usb_endpoint_descriptor_t* tx_ep = nullptr;
  usb_endpoint_descriptor_t* rx_ep = nullptr;
  usb_interface_descriptor_t* default_ifc = nullptr;
  usb_interface_descriptor_t* data_ifc = nullptr;
  ecm_ctx_t ecm_ctx;
  usb_protocol_t* usb = GetUsbProto();
  ecm_ctx.usb = *usb;

  ASSERT_OK(usb_desc_iter_init(usb, &iter));
  ASSERT_OK(
      parse_usb_descriptor(&iter, &int_ep, &tx_ep, &rx_ep, &default_ifc, &data_ifc, &ecm_ctx));
}

}  // namespace
