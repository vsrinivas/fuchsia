// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/status.h>

#include <cstdint>
#include <iterator>

#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <virtio/input.h>

#include "src/devices/bus/lib/virtio/trace.h"
#include "src/ui/input/drivers/virtio/input.h"

#define LOCAL_TRACE 0

namespace virtio {

namespace {

constexpr uint16_t kKeyCodeBtnLeft = 0x110;    // BTN_LEFT
constexpr uint16_t kKeyCodeBtnRight = 0x111;   // BTN_RIGHT
constexpr uint16_t kKeyCodeBtnMiddle = 0x112;  // BTN_MIDDLE

constexpr uint8_t kButtonIndexLeft = 1;
constexpr uint8_t kButtonIndexRight = 2;
constexpr uint8_t kButtonIndexMid = 3;

}  // namespace

zx_status_t HidMouse::GetDescriptor(uint8_t desc_type, void* out_data_buffer, size_t data_size,
                                    size_t* out_data_actual) {
  if (out_data_buffer == nullptr || out_data_actual == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (desc_type != HID_DESCRIPTION_TYPE_REPORT) {
    return ZX_ERR_NOT_FOUND;
  }

  size_t report_size = 0u;
  const uint8_t* report_descriptor = get_virtio_scroll_mouse_report_desc(&report_size);

  if (data_size < report_size) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  memcpy(out_data_buffer, report_descriptor, report_size);
  *out_data_actual = report_size;
  return ZX_OK;
}

void HidMouse::ReceiveKeyEvent(virtio_input_event_t* event) {
  ZX_DEBUG_ASSERT(event->type == VIRTIO_INPUT_EV_KEY);
  uint16_t key_code = event->code;
  uint32_t status = event->value;

  uint8_t button_idx = 0;
  switch (key_code) {
    case kKeyCodeBtnLeft:
      button_idx = kButtonIndexLeft;
      break;
    case kKeyCodeBtnRight:
      button_idx = kButtonIndexRight;
      break;
    case kKeyCodeBtnMiddle:
      button_idx = kButtonIndexMid;
      break;
    default:
      zxlogf(ERROR, "%s: key code %u not supported!", __func__, key_code);
      return;
  }

  if (status == VIRTIO_INPUT_EV_KEY_PRESSED) {
    report_.button |= 1 << (button_idx - 1);
  } else {
    report_.button &= ~(1 << (button_idx - 1));
  }
}

void HidMouse::ReceiveRelEvent(virtio_input_event_t* event) {
  ZX_DEBUG_ASSERT(event->type == VIRTIO_INPUT_EV_REL);
  switch (event->code) {
    case VIRTIO_INPUT_EV_REL_X:
      report_.rel_x = static_cast<int16_t>(event->value);
      break;
    case VIRTIO_INPUT_EV_REL_Y:
      report_.rel_y = static_cast<int16_t>(event->value);
      break;
    case VIRTIO_INPUT_EV_REL_WHEEL:
      report_.rel_wheel = static_cast<int16_t>(event->value);
      break;
    default:
      zxlogf(ERROR, "%s: event code %u not supported!", __func__, event->code);
      return;
  }
}

void HidMouse::ReceiveEvent(virtio_input_event_t* event) {
  switch (event->type) {
    case VIRTIO_INPUT_EV_KEY:
      ReceiveKeyEvent(event);
      break;
    case VIRTIO_INPUT_EV_REL:
      ReceiveRelEvent(event);
      break;
    case VIRTIO_INPUT_EV_SYN:
      // EV_SYN events will be handled by InputDevice directly after calling
      // |ReceiveEvent|, so we ignore the SYN event here.
      break;
    default:
      zxlogf(ERROR, "%s: unsupported event type %u!", __func__, event->type);
      break;
  }
}

const uint8_t* HidMouse::GetReport(size_t* size) {
  *size = sizeof(report_);
  return reinterpret_cast<const uint8_t*>(&report_);
}

}  // namespace virtio
