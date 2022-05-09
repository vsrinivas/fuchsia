// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/drivers/pc-ps2/device.h"

#include <fidl/fuchsia.hardware.input/cpp/wire.h>
#include <fidl/fuchsia.hardware.input/cpp/wire_types.h>
#include <fuchsia/hardware/hidbus/cpp/banjo.h>
#include <lib/ddk/driver.h>
#include <zircon/syscalls.h>

#include <sstream>

#include <ddktl/device.h>
#include <hid/boot.h>

#include "src/ui/input/drivers/pc-ps2/commands.h"
#include "src/ui/input/drivers/pc-ps2/controller.h"
#include "src/ui/input/drivers/pc-ps2/descriptors.h"
#include "src/ui/input/drivers/pc-ps2/keymap.h"

#ifdef PS2_TEST
extern zx::interrupt GetInterrupt(uint32_t irq);
#endif

namespace i8042 {

namespace finput = fuchsia_hardware_input::wire;

namespace {

constexpr uint16_t kIrqPort1 = 0x1;
constexpr uint16_t kIrqPort2 = 0xc;

constexpr uint8_t kMouseAlwaysOne = (1 << 3);
constexpr uint8_t kMouseButtonMask = 0x7;

struct PortInfo {
  Command enable;
  Command disable;
  uint32_t irq;
  const char* devname;
};

__UNUSED constexpr PortInfo kPortInfo[2] = {
    /*[kPort1] =*/
    PortInfo{
        .enable = kCmdPort1Enable,
        .disable = kCmdPort1Disable,
        .irq = kIrqPort1,
        .devname = "i8042-keyboard",
    },
    /*[kPort2] =*/
    PortInfo{
        .enable = kCmdPort2Enable,
        .disable = kCmdPort2Disable,
        .irq = kIrqPort2,
        .devname = "i8042-mouse",
    },
};

constexpr hid_boot_kbd_report_t kRolloverReport = {.modifier = 1, .usage = {1, 1, 1, 1, 1, 1}};
bool IsKeyboardModifier(uint8_t usage) {
  return (usage >= HID_USAGE_KEY_LEFT_CTRL && usage <= HID_USAGE_KEY_RIGHT_GUI);
}

}  // namespace

zx_status_t I8042Device::Bind(Controller* parent, Port port) {
  auto dev = std::make_unique<I8042Device>(parent, port);
  zx_status_t status = dev->Bind();
  if (status == ZX_OK) {
    // The DDK takes ownership of the device.
    __UNUSED auto unused = dev.release();
  }

  return status;
}

zx_status_t I8042Device::Bind() {
  auto identity = Identify();
  if (identity.is_error()) {
    zxlogf(ERROR, "Identify failed: %s", identity.status_string());
    return identity.error_value();
  }

  protocol_ = *identity;
#ifndef PS2_TEST
  // Map interrupt. We should get this from ACPI eventually.
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx_status_t status = zx::interrupt::create(*zx::unowned_resource(get_root_resource()),
                                             kPortInfo[port_].irq, ZX_INTERRUPT_REMAP_IRQ, &irq_);
  if (status != ZX_OK) {
    return status;
  }
#else
  irq_ = GetInterrupt(kPortInfo[port_].irq);
  zx_status_t status;
#endif

  status = DdkAdd(ddk::DeviceAddArgs(kPortInfo[port_].devname));
  if (status != ZX_OK) {
    return status;
  }

  // Start the IRQ thread.
  irq_thread_ = std::thread([this]() { IrqThread(); });

  return ZX_OK;
}

void I8042Device::DdkUnbind(ddk::UnbindTxn txn) {
  if (!irq_thread_.joinable()) {
    txn.Reply();
    return;
  }
  {
    std::scoped_lock lock(unbind_lock_);
    unbind_.emplace(std::move(txn));
  }
  // Destroy the IRQ, causing the IRQ handler to finish.
  irq_.destroy();
  unbind_ready_.notify_all();
}

zx::status<finput::BootProtocol> I8042Device::Identify() {
  // Before sending IDENTIFY, disable scanning.
  // Otherwise a keyboard button pressed by the user could interfere with the value returned by
  // IDENTIFY.
  auto ret = controller_->SendDeviceCommand(kCmdDeviceScanDisable, port_);
  if (ret.is_error()) {
    zxlogf(ERROR, "Disable scan failed: %s", ret.status_string());
    return ret.take_error();
  }
  if (ret->empty() || ret.value()[0] != kAck) {
    zxlogf(ERROR, "Disable scan failed: bad response (size = %zu, first value = 0x%x)", ret->size(),
           ret->empty() ? -1 : ret.value()[0]);
    return zx::error(ZX_ERR_IO);
  }

  ret = controller_->SendDeviceCommand(kCmdDeviceIdentify, port_);
  if (ret.is_error()) {
    zxlogf(ERROR, "Identify failed: %s", ret.status_string());
    return ret.take_error();
  }
  if (ret->empty() || ret.value()[0] != kAck) {
    zxlogf(ERROR, "Identify failed: bad response");
    return zx::error(ZX_ERR_IO);
  }

  auto& ident = ret.value();
  if (ident.size() == 1) {
    zxlogf(WARNING, "i8042 device has no identity?");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  std::ostringstream buf;
  for (size_t i = 1; i < ident.size(); i++) {
    buf << "0x" << std::hex << static_cast<int>(ident[i]) << ", ";
  }

  auto str = buf.str();
  zxlogf(INFO, "Identify: %s", str.empty() ? "(no response)" : str.data());

  finput::BootProtocol proto = finput::BootProtocol::kNone;
  if (ident[1] == 0xab) {
    proto = finput::BootProtocol::kKbd;
  } else {
    proto = finput::BootProtocol::kMouse;
  }

  // Re-enable the device.
  ret = controller_->SendDeviceCommand(kCmdDeviceScanEnable, port_);
  if (ret.is_error()) {
    return ret.take_error();
  }
  if (ret->empty() || ret.value()[0] != kAck) {
    return zx::error(ZX_ERR_IO);
  }
  return zx::ok(proto);
}

zx_status_t I8042Device::HidbusQuery(uint32_t options, hid_info_t* out_info) {
  out_info->dev_num = static_cast<uint8_t>(protocol_);
  out_info->device_class = static_cast<hid_device_class_t>(protocol_);
  out_info->boot_device = true;
  return ZX_OK;
}

zx_status_t I8042Device::HidbusStart(const hidbus_ifc_protocol_t* ifc) {
  std::scoped_lock lock(hid_lock_);
  if (ifc_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  ifc_ = ddk::HidbusIfcProtocolClient(ifc);
  return ZX_OK;
}

void I8042Device::HidbusStop() {
  std::scoped_lock lock(hid_lock_);
  ifc_.clear();
}

zx_status_t I8042Device::HidbusGetDescriptor(hid_description_type_t desc_type,
                                             uint8_t* out_data_buffer, size_t data_size,
                                             size_t* out_data_actual) {
  if (out_data_buffer == NULL || out_data_actual == NULL) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (desc_type != HID_DESCRIPTION_TYPE_REPORT) {
    return ZX_ERR_NOT_FOUND;
  }

  const uint8_t* buf;
  size_t buflen = 0;
  if (protocol_ == finput::BootProtocol::kKbd) {
    buf = kKeyboardHidDescriptor;
    buflen = sizeof(kKeyboardHidDescriptor);
  } else if (protocol_ == finput::BootProtocol::kMouse) {
    buf = kMouseHidDescriptor;
    buflen = sizeof(kMouseHidDescriptor);
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }

  *out_data_actual = buflen;
  if (data_size < buflen) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  memcpy(out_data_buffer, buf, buflen);
  return ZX_OK;
}

void I8042Device::IrqThread() {
  while (true) {
    zx_status_t status = irq_.wait(nullptr);
    if (status != ZX_OK) {
      break;
    }

    bool retry;
    do {
      retry = false;

      auto status = controller_->ReadStatus();
      if (status.obf()) {
        retry = true;
        uint8_t data = controller_->ReadData();
        if (protocol_ == finput::BootProtocol::kKbd) {
          ProcessScancode(data);
        } else if (protocol_ == finput::BootProtocol::kMouse) {
          ProcessMouse(data);
        }
      }
    } while (retry);
  }

  std::scoped_lock lock(unbind_lock_);
  unbind_ready_.wait(unbind_lock_,
                     [this]() __TA_REQUIRES(unbind_lock_) { return unbind_.has_value(); });
  unbind_->Reply();
}

void I8042Device::ProcessScancode(uint8_t code) {
  bool multi = (last_code_ == kExtendedScancode);
  last_code_ = code;

  bool key_up = !!(code & kKeyUp);
  code &= kScancodeMask;

  uint8_t usage;
  if (multi) {
    usage = kSet1ExtendedUsageMap[code];
  } else {
    usage = kSet1UsageMap[code];
  }

  bool rollover = false;
  if (IsKeyboardModifier(usage)) {
    switch (ModifierKey(usage, !key_up)) {
      case ModStatus::kExists:
        return;
      case ModStatus::kRollover:
        rollover = true;
        break;
      case ModStatus::kSet:
      default:
        break;
    }
  } else if (key_up) {
    RemoveKey(usage);
  } else {
    AddKey(usage);
  }

  const hid_boot_kbd_report_t* report = rollover ? &kRolloverReport : &keyboard_report();

  {
    std::scoped_lock lock(hid_lock_);
    if (ifc_.is_valid()) {
      ifc_.IoQueue(reinterpret_cast<const uint8_t*>(report), sizeof(*report),
                   zx_clock_get_monotonic());
    }
  }
}

ModStatus I8042Device::ModifierKey(uint8_t usage, bool down) {
  int bit = usage - HID_USAGE_KEY_LEFT_CTRL;
  if (bit < 0 || bit > 7)
    return ModStatus::kRollover;
  if (down) {
    if (keyboard_report().modifier & 1 << bit) {
      return ModStatus::kExists;
    }
    keyboard_report().modifier |= 1 << bit;

  } else {
    keyboard_report().modifier &= ~(1 << bit);
  }
  return ModStatus::kSet;
}

KeyStatus I8042Device::AddKey(uint8_t usage) {
  for (unsigned char& key : keyboard_report().usage) {
    if (key == usage) {
      return KeyStatus::kKeyExists;
    }
    if (key == 0) {
      key = usage;
      return KeyStatus::kKeyAdded;
    }
  }
  return KeyStatus::kKeyRollover;
}

KeyStatus I8042Device::RemoveKey(uint8_t usage) {
  ssize_t idx = -1;
  for (size_t i = 0; i < sizeof(keyboard_report().usage); i++) {
    if (keyboard_report().usage[i] == usage) {
      idx = i;
      break;
    }
  }

  if (idx == -1) {
    return KeyStatus::kKeyNotFound;
  }

  for (size_t i = idx; i < sizeof(keyboard_report().usage) - 1; i++) {
    keyboard_report().usage[i] = keyboard_report().usage[i + 1];
  }
  keyboard_report().usage[sizeof(keyboard_report().usage) - 1] = 0;
  return KeyStatus::kKeyRemoved;
}

void I8042Device::ProcessMouse(uint8_t code) {
  // PS/2 mouse reports span 3 bytes. last_code_ tracks which byte we're up to.
  switch (last_code_) {
    case 0:
      // The first byte should always have this bit set. If it's not set, ignore the packet.
      if (!(code & kMouseAlwaysOne)) {
        return;
      }
      mouse_report().buttons = code;
      break;
    case 1: {
      int state = mouse_report().buttons;
      int d = code;
      mouse_report().rel_x = static_cast<int8_t>(d - ((state << 4) & 0x100));
      break;
    }
    case 2: {
      int state = mouse_report().buttons;
      int d = code;
      // PS/2 maps the y-axis backwards so invert the rel_y value
      mouse_report().rel_y = static_cast<int8_t>(((state << 3) & 0x100) - d);
      mouse_report().buttons &= kMouseButtonMask;

      std::scoped_lock lock(hid_lock_);
      if (ifc_.is_valid()) {
        ifc_.IoQueue(reinterpret_cast<const uint8_t*>(&mouse_report()), sizeof(mouse_report()),
                     zx_clock_get_monotonic());
      }

      memset(&mouse_report(), 0, sizeof(mouse_report()));
      break;
    }
  }

  last_code_ = (last_code_ + 1) % 3;
}

}  // namespace i8042
