// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/drivers/pc-ps2/device.h"

#include <fidl/fuchsia.hardware.input/cpp/wire.h>
#include <fidl/fuchsia.hardware.input/cpp/wire_types.h>
#include <lib/ddk/driver.h>
#include <zircon/syscalls.h>

#include <map>
#include <sstream>

#include <ddktl/device.h>
#include <hid/boot.h>

#include "src/ui/input/drivers/pc-ps2/commands.h"
#include "src/ui/input/drivers/pc-ps2/controller.h"
#include "src/ui/input/drivers/pc-ps2/keymap.h"

#ifdef PS2_TEST
extern zx::interrupt GetInterrupt(uint32_t irq);
#endif

namespace i8042 {

namespace finput = fuchsia_hardware_input::wire;

namespace {

inline const std::map<uint8_t, fuchsia_input::wire::Key> kUndefinedExtendedUsageMap = {
    {0x58, fuchsia_input::wire::Key::kAssistant}, {0x5e, fuchsia_input::wire::Key::kPower}};

constexpr uint16_t kIrqPort1 = 0x1;
constexpr uint16_t kIrqPort2 = 0xc;

constexpr uint8_t kMouseButtonCount = 3;
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

}  // namespace

void PS2InputReport::ToFidlInputReport(
    fidl::WireTableBuilder<::fuchsia_input_report::wire::InputReport>& input_report,
    fidl::AnyArena& allocator) {
  if (type == fuchsia_hardware_input::BootProtocol::kKbd) {
    ZX_ASSERT(std::holds_alternative<PS2KbdInputReport>(report));
    auto kbd = std::get<PS2KbdInputReport>(report);
    fidl::VectorView<fuchsia_input::wire::Key> keys3(allocator, kbd.num_pressed_keys_3);
    size_t idx = 0;
    for (const auto& key : kbd.pressed_keys_3) {
      keys3[idx++] = key;
    }

    auto kbd_input_rpt = fuchsia_input_report::wire::KeyboardInputReport::Builder(allocator);
    kbd_input_rpt.pressed_keys3(keys3);

    input_report.keyboard(kbd_input_rpt.Build());
  } else if (type == fuchsia_hardware_input::BootProtocol::kMouse) {
    ZX_ASSERT(std::holds_alternative<PS2MouseInputReport>(report));
    auto mouse = std::get<PS2MouseInputReport>(report);
    std::vector<uint8_t> pressed_buttons;
    for (uint8_t i = 0; i < kMouseButtonCount; i++) {
      if (mouse.buttons & (1 << i)) {
        pressed_buttons.push_back(i + 1);
      }
    }
    fidl::VectorView<uint8_t> buttons(allocator, pressed_buttons.size());
    size_t idx = 0;
    for (const auto& button : pressed_buttons) {
      buttons[idx++] = button;
    }

    auto mouse_input_rpt = fuchsia_input_report::wire::MouseInputReport::Builder(allocator);
    mouse_input_rpt.pressed_buttons(buttons);
    mouse_input_rpt.movement_x(mouse.rel_x);
    mouse_input_rpt.movement_y(mouse.rel_y);

    input_report.mouse(mouse_input_rpt.Build());
  }

  input_report.event_time(event_time.get());
}

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
  if (protocol_ == fuchsia_hardware_input::BootProtocol::kKbd) {
    report_.report = PS2KbdInputReport{};
  } else if (protocol_ == fuchsia_hardware_input::BootProtocol::kMouse) {
    report_.report = PS2MouseInputReport{};
  }

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

  status = loop_.StartThread("i8042-reader-thread");
  if (status != ZX_OK) {
    return status;
  }

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

zx::result<finput::BootProtocol> I8042Device::Identify() {
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

void I8042Device::GetInputReportsReader(GetInputReportsReaderRequestView request,
                                        GetInputReportsReaderCompleter::Sync& completer) {
  std::scoped_lock lock(hid_lock_);
  zx_status_t status =
      input_report_readers_.CreateReader(loop_.dispatcher(), std::move(request->reader));
  if (status == ZX_OK) {
#ifdef PS2_TEST
    sync_completion_signal(&next_reader_wait_);
#endif
  }
}

void I8042Device::GetDescriptor(GetDescriptorCompleter::Sync& completer) {
  fidl::Arena allocator;
  auto descriptor = fuchsia_input_report::wire::DeviceDescriptor::Builder(allocator);

  fuchsia_input_report::wire::DeviceInfo device_info;
  device_info.vendor_id = static_cast<uint32_t>(fuchsia_input_report::wire::VendorId::kGoogle);

  if (protocol_ == fuchsia_hardware_input::BootProtocol::kKbd) {
    device_info.product_id =
        static_cast<uint32_t>(fuchsia_input_report::wire::VendorGoogleProductId::kPcPs2Keyboard);
    std::vector<fuchsia_input::wire::Key> keys3;
    // Add usual HID keys
    for (const auto& key : kSet1UsageMap) {
      if (key) {
        keys3.push_back(key.value());
      }

      if (keys3.size() >= fuchsia_input_report::wire::kKeyboardMaxNumKeys) {
        zxlogf(ERROR, "Too many keys!");
        completer.Reply({});
        return;
      }
    }
    for (const auto& key : kSet1ExtendedUsageMap) {
      if (key) {
        keys3.push_back(key.value());
      }

      if (keys3.size() >= fuchsia_input_report::wire::kKeyboardMaxNumKeys) {
        zxlogf(ERROR, "Too many keys!");
        completer.Reply({});
        return;
      }
    }

    // Add implementation specific keys. If needed, can be passed in from metadata
    for (auto const& [scancode, key] : kUndefinedExtendedUsageMap) {
      keys3.push_back(key);

      if (keys3.size() >= fuchsia_input_report::wire::kKeyboardMaxNumKeys) {
        zxlogf(ERROR, "Too many keys!");
        completer.Reply({});
        return;
      }
    }

    fidl::VectorView<fuchsia_input::wire::Key> fidl_keys3(allocator, keys3.size());
    size_t idx = 0;
    for (const auto& key : keys3) {
      fidl_keys3[idx++] = key;
    }

    auto kbd_in_desc = fuchsia_input_report::wire::KeyboardInputDescriptor::Builder(allocator);
    kbd_in_desc.keys3(fidl_keys3);

    fidl::VectorView<fuchsia_input_report::wire::LedType> leds(allocator, 5);
    leds[0] = fuchsia_input_report::wire::LedType::kNumLock;
    leds[1] = fuchsia_input_report::wire::LedType::kCapsLock;
    leds[2] = fuchsia_input_report::wire::LedType::kScrollLock;
    leds[3] = fuchsia_input_report::wire::LedType::kCompose;
    leds[4] = fuchsia_input_report::wire::LedType::kKana;
    auto kbd_out_desc = fuchsia_input_report::wire::KeyboardOutputDescriptor::Builder(allocator);
    kbd_out_desc.leds(leds);

    auto kbd_descriptor = fuchsia_input_report::wire::KeyboardDescriptor::Builder(allocator);
    kbd_descriptor.input(kbd_in_desc.Build());
    kbd_descriptor.output(kbd_out_desc.Build());
    descriptor.keyboard(kbd_descriptor.Build());
  } else if (protocol_ == fuchsia_hardware_input::BootProtocol::kMouse) {
    device_info.product_id =
        static_cast<uint32_t>(fuchsia_input_report::wire::VendorGoogleProductId::kPcPs2Mouse);
    fidl::VectorView<uint8_t> buttons(allocator, kMouseButtonCount);
    buttons[0] = 0x01;
    buttons[1] = 0x02;
    buttons[2] = 0x03;

    constexpr fuchsia_input_report::wire::Axis movement_x{
        .range = {.min = -127, .max = 127},
        .unit = {.type = fuchsia_input_report::wire::UnitType::kNone, .exponent = 0},
    };
    constexpr fuchsia_input_report::wire::Axis movement_y{
        .range = {.min = -127, .max = 127},
        .unit = {.type = fuchsia_input_report::wire::UnitType::kNone, .exponent = 0},
    };

    auto mouse_in_desc = fuchsia_input_report::wire::MouseInputDescriptor::Builder(allocator);
    mouse_in_desc.buttons(buttons);
    mouse_in_desc.movement_x(movement_x);
    mouse_in_desc.movement_y(movement_y);

    auto mouse_descriptor = fuchsia_input_report::wire::MouseDescriptor::Builder(allocator);
    mouse_descriptor.input(mouse_in_desc.Build());
    descriptor.mouse(mouse_descriptor.Build());
  }
  descriptor.device_info(device_info);

  completer.Reply(descriptor.Build());
}

void I8042Device::IrqThread() {
  while (true) {
    zx::time timestamp;
    zx_status_t status = irq_.wait(&timestamp);
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
          ProcessScancode(timestamp, data);
        } else if (protocol_ == finput::BootProtocol::kMouse) {
          ProcessMouse(timestamp, data);
        }
      }
    } while (retry);
  }

  std::scoped_lock lock(unbind_lock_);
  unbind_ready_.wait(unbind_lock_,
                     [this]() __TA_REQUIRES(unbind_lock_) { return unbind_.has_value(); });
  unbind_->Reply();
}

void I8042Device::ProcessScancode(zx::time timestamp, uint8_t code) {
  report_.event_time = timestamp;
  report_.type = fuchsia_hardware_input::wire::BootProtocol::kKbd;

  bool multi = (last_code_ == kExtendedScancode);
  last_code_ = code;

  bool key_up = !!(code & kKeyUp);
  code &= kScancodeMask;

  std::optional<fuchsia_input::wire::Key> key;
  if (multi) {
    key = kSet1ExtendedUsageMap[code];
  } else {
    key = kSet1UsageMap[code];
  }
  if (!key) {
    auto it = kUndefinedExtendedUsageMap.find(code);
    if (it == kUndefinedExtendedUsageMap.end()) {
      return;
    }
    key = it->second;
  }

  if (key_up) {
    RemoveKey(*key);
  } else {
    AddKey(*key);
  }

  {
    std::scoped_lock lock(hid_lock_);
    input_report_readers_.SendReportToAllReaders(report_);
  }
}

KeyStatus I8042Device::AddKey(fuchsia_input::wire::Key key) {
  for (size_t i = 0; i < keyboard_report().num_pressed_keys_3; i++) {
    if (keyboard_report().pressed_keys_3[i] == key) {
      return KeyStatus::kKeyExists;
    }
  }
  keyboard_report().pressed_keys_3[keyboard_report().num_pressed_keys_3++] = key;
  return KeyStatus::kKeyAdded;
}

KeyStatus I8042Device::RemoveKey(fuchsia_input::wire::Key key) {
  size_t idx = -1;
  for (size_t i = 0; i < keyboard_report().num_pressed_keys_3; i++) {
    if (keyboard_report().pressed_keys_3[i] == key) {
      idx = i;
      break;
    }
  }

  if (idx == -1UL) {
    return KeyStatus::kKeyNotFound;
  }

  for (size_t i = idx; i < keyboard_report().num_pressed_keys_3 - 1; i++) {
    keyboard_report().pressed_keys_3[i] = keyboard_report().pressed_keys_3[i + 1];
  }
  keyboard_report().num_pressed_keys_3--;
  return KeyStatus::kKeyRemoved;
}

void I8042Device::ProcessMouse(zx::time timestamp, uint8_t code) {
  report_.type = fuchsia_hardware_input::wire::BootProtocol::kMouse;
  report_.event_time = timestamp;
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
      input_report_readers_.SendReportToAllReaders(report_);
      report_.Reset();
      break;
    }
  }

  last_code_ = (last_code_ + 1) % 3;
}

}  // namespace i8042
