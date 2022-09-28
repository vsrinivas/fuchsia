// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-cdc-ecm-lib.h"

struct FuzzInput {
  const uint8_t* data;
  const size_t size;
};

static void UsbGetDescriptors(void* ctx, uint8_t* out_descs_buffer, size_t descs_size,
                              size_t* out_descs_actual) {
  auto input = reinterpret_cast<FuzzInput*>(ctx);
  size_t descriptors_length = input->size;
  if (descs_size < descriptors_length) {
    descriptors_length = descs_size;
  }
  memcpy(out_descs_buffer, input->data, descriptors_length);
  *out_descs_actual = descriptors_length;
}

static size_t UsbGetDescriptorsLength(void* ctx) {
  auto input = reinterpret_cast<FuzzInput*>(ctx);
  return input->size;
}

static zx_status_t UsbControlIn(void* ctx, uint8_t request_type, uint8_t request, uint16_t value,
                                uint16_t index, int64_t timeout, uint8_t* out_read_buffer,
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
  usb_str.b_length = expected_str_size;
  usb_str.b_descriptor_type = USB_DT_STRING;
  memcpy(out_read_buffer, &usb_str, sizeof(usb_string_descriptor_t));
  uint8_t* ptr = reinterpret_cast<uint8_t*>(out_read_buffer) + sizeof(usb_string_descriptor_t);
  for (size_t i = 0; i < ETH_MAC_SIZE; i++) {
    memcpy(ptr, "F\0F\0", 4);
    ptr += 4;
  }

  return ZX_OK;
}

usb_protocol_ops_t kFuzzedUsbProtocolOps = {
    .control_in = UsbControlIn,
    .get_descriptors_length = UsbGetDescriptorsLength,
    .get_descriptors = UsbGetDescriptors,
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
  FuzzInput input = {.data = Data, .size = Size};
  usb_protocol_t usb = {.ops = &kFuzzedUsbProtocolOps, .ctx = &input};

  __UNUSED auto parser = usb_cdc_ecm::UsbCdcDescriptorParser::Parse(&usb);
  return 0;
}
