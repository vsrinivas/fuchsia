// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gt6853.h"

#include <endian.h>
#include <lib/zx/profile.h>
#include <threads.h>
#include <zircon/threads.h>

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/composite.h>
#include <fbl/auto_lock.h>

#include "src/ui/input/drivers/gt6853/gt6853-bind.h"

namespace {

constexpr int64_t kMaxContactX = 600;
constexpr int64_t kMaxContactY = 1024;

constexpr size_t kContactSize = 8;

constexpr uint8_t kTouchEvent = 1 << 7;

}  // namespace

namespace touch {

enum class Gt6853Device::Register : uint16_t {
  kEventStatusReg = 0x4100,
  kContactsReg = 0x4101,
  kContactsStartReg = 0x4102,
};

void Gt6853InputReport::ToFidlInputReport(fuchsia_input_report::InputReport::Builder& builder,
                                          fidl::Allocator& allocator) {
  auto input_contacts = allocator.make<fuchsia_input_report::ContactInputReport[]>(num_contacts);
  for (size_t i = 0; i < num_contacts; i++) {
    auto contact = fuchsia_input_report::ContactInputReport::Builder(
        allocator.make<fuchsia_input_report::ContactInputReport::Frame>());
    contact.set_contact_id(allocator.make<uint32_t>(contacts[i].contact_id));
    contact.set_position_x(allocator.make<int64_t>(contacts[i].position_x));
    contact.set_position_y(allocator.make<int64_t>(contacts[i].position_y));
    input_contacts[i] = contact.build();
  }

  auto touch_report =
      fuchsia_input_report::TouchInputReport::Builder(
          allocator.make<fuchsia_input_report::TouchInputReport::Frame>())
          .set_contacts(allocator.make<fidl::VectorView<fuchsia_input_report::ContactInputReport>>(
              std::move(input_contacts), num_contacts));

  auto time = allocator.make<zx_time_t>(event_time.get());
  builder.set_event_time(std::move(time))
      .set_touch(allocator.make<fuchsia_input_report::TouchInputReport>(touch_report.build()));
}

zx::status<Gt6853Device*> Gt6853Device::CreateAndGetDevice(void* ctx, zx_device_t* parent) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "Failed to get composite protocol");
    return zx::error(ZX_ERR_NO_RESOURCES);
  }

  zx_device_t* i2c = {};
  if (!composite.GetFragment("i2c", &i2c)) {
    zxlogf(ERROR, "Failed to get I2C fragment");
    return zx::error(ZX_ERR_NO_RESOURCES);
  }

  zx_device_t* interrupt_gpio = {};
  if (!composite.GetFragment("gpio-int", &interrupt_gpio)) {
    zxlogf(ERROR, "Failed to get interrupt GPIO fragment");
    return zx::error(ZX_ERR_NO_RESOURCES);
  }

  zx_device_t* reset_gpio = {};
  if (!composite.GetFragment("gpio-reset", &reset_gpio)) {
    zxlogf(ERROR, "Failed to get reset GPIO fragment");
    return zx::error(ZX_ERR_NO_RESOURCES);
  }

  std::unique_ptr<Gt6853Device> device =
      std::make_unique<Gt6853Device>(parent, i2c, interrupt_gpio, reset_gpio);
  if (!device) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  zx_status_t status = device->Init();
  if (status != ZX_OK) {
    return zx::error(status);
  }

  if ((status = device->DdkAdd("gt6853")) != ZX_OK) {
    zxlogf(ERROR, "DdkAdd failed: %d", status);
    return zx::error(status);
  }

  return zx::ok(device.release());
}

zx_status_t Gt6853Device::Create(void* ctx, zx_device_t* parent) {
  auto status = CreateAndGetDevice(ctx, parent);
  return status.is_error() ? status.error_value() : ZX_OK;
}

zx_status_t Gt6853Device::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fuchsia_input_report::InputDevice::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void Gt6853Device::DdkUnbind(ddk::UnbindTxn txn) {
  Shutdown();
  txn.Reply();
}

void Gt6853Device::GetInputReportsReader(zx::channel server,
                                         GetInputReportsReaderCompleter::Sync& completer) {
  zx_status_t status = input_report_readers_.CreateReader(loop_.dispatcher(), std::move(server));
  if (status == ZX_OK) {
    sync_completion_signal(&next_reader_wait_);  // Only for tests.
  }
}

void Gt6853Device::GetDescriptor(GetDescriptorCompleter::Sync& completer) {
  constexpr size_t kDescriptorBufferSize = 512;

  constexpr fuchsia_input_report::Axis kAxisX = {
      .range = {.min = 0, .max = kMaxContactX},
      .unit = {.type = fuchsia_input_report::UnitType::NONE, .exponent = 0},
  };

  constexpr fuchsia_input_report::Axis kAxisY = {
      .range = {.min = 0, .max = kMaxContactY},
      .unit = {.type = fuchsia_input_report::UnitType::NONE, .exponent = 0},
  };

  fidl::BufferThenHeapAllocator<kDescriptorBufferSize> allocator;

  fuchsia_input_report::DeviceInfo device_info;
  device_info.vendor_id = static_cast<uint32_t>(fuchsia_input_report::VendorId::GOOGLE);
  device_info.product_id =
      static_cast<uint32_t>(fuchsia_input_report::VendorGoogleProductId::FOCALTECH_TOUCHSCREEN);

  auto touch_input_contacts =
      allocator.make<fuchsia_input_report::ContactInputDescriptor[]>(kMaxContacts);
  for (uint32_t i = 0; i < kMaxContacts; i++) {
    touch_input_contacts[i] =
        fuchsia_input_report::ContactInputDescriptor::Builder(
            allocator.make<fuchsia_input_report::ContactInputDescriptor::Frame>())
            .set_position_x(allocator.make<fuchsia_input_report::Axis>(kAxisX))
            .set_position_y(allocator.make<fuchsia_input_report::Axis>(kAxisY))
            .build();
  }

  auto touch_input_descriptor =
      fuchsia_input_report::TouchInputDescriptor::Builder(
          allocator.make<fuchsia_input_report::TouchInputDescriptor::Frame>())
          .set_contacts(
              allocator.make<fidl::VectorView<fuchsia_input_report::ContactInputDescriptor>>(
                  std::move(touch_input_contacts), kMaxContacts))
          .set_max_contacts(allocator.make<uint32_t>(kMaxContacts))
          .set_touch_type(allocator.make<fuchsia_input_report::TouchType>(
              fuchsia_input_report::TouchType::TOUCHSCREEN));

  auto touch_descriptor = fuchsia_input_report::TouchDescriptor::Builder(
                              allocator.make<fuchsia_input_report::TouchDescriptor::Frame>())
                              .set_input(allocator.make<fuchsia_input_report::TouchInputDescriptor>(
                                  touch_input_descriptor.build()));

  auto descriptor =
      fuchsia_input_report::DeviceDescriptor::Builder(
          allocator.make<fuchsia_input_report::DeviceDescriptor::Frame>())
          .set_device_info(allocator.make<fuchsia_input_report::DeviceInfo>(device_info))
          .set_touch(
              allocator.make<fuchsia_input_report::TouchDescriptor>(touch_descriptor.build()));

  completer.Reply(descriptor.build());
}

void Gt6853Device::SendOutputReport(fuchsia_input_report::OutputReport report,
                                    SendOutputReportCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Gt6853Device::GetFeatureReport(GetFeatureReportCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Gt6853Device::SetFeatureReport(fuchsia_input_report::FeatureReport report,
                                    SetFeatureReportCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Gt6853Device::WaitForNextReader() {
  sync_completion_wait(&next_reader_wait_, ZX_TIME_INFINITE);
  sync_completion_reset(&next_reader_wait_);
}

Gt6853Contact Gt6853Device::ParseContact(const uint8_t* const contact_buffer) {
  Gt6853Contact ret = {};
  ret.contact_id = contact_buffer[0] & 0b1111;
  ret.position_x = contact_buffer[1] | (contact_buffer[2] << 8);
  ret.position_y = contact_buffer[3] | (contact_buffer[4] << 8);
  return ret;
}

zx_status_t Gt6853Device::Init() {
  zx_status_t status = interrupt_gpio_.ConfigIn(GPIO_NO_PULL);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ConfigIn failed: %d", status);
    return status;
  }

  if ((status = interrupt_gpio_.GetInterrupt(ZX_INTERRUPT_MODE_EDGE_LOW, &interrupt_)) != ZX_OK) {
    zxlogf(ERROR, "GetInterrupt failed: %d", status);
    return status;
  }

  status = thrd_create_with_name(
      &thread_, [](void* arg) -> int { return reinterpret_cast<Gt6853Device*>(arg)->Thread(); },
      this, "gt6853-thread");
  if (status != thrd_success) {
    zxlogf(ERROR, "Failed to create thread: %d", status);
    return thrd_status_to_zx_status(status);
  }

  // Copied from //src/ui/input/drivers/focaltech/ft_device.cc

  // Set profile for device thread.
  // TODO(fxbug.dev/40858): Migrate to the role-based API when available, instead of hard
  // coding parameters.
  {
    const zx::duration capacity = zx::usec(200);
    const zx::duration deadline = zx::msec(1);
    const zx::duration period = deadline;

    zx::profile profile;
    status = device_get_deadline_profile(zxdev(), capacity.get(), deadline.get(), period.get(),
                                         "gt6853-thread", profile.reset_and_get_address());
    if (status != ZX_OK) {
      zxlogf(WARNING, "Failed to get deadline profile: %d", status);
    } else {
      status = zx_object_set_profile(thrd_get_zx_handle(thread_), profile.get(), 0);
      if (status != ZX_OK) {
        zxlogf(WARNING, "Failed to apply deadline profile to device thread: %d", status);
      }
    }
  }

  if ((status = loop_.StartThread("gt6853-reader-thread")) != ZX_OK) {
    zxlogf(ERROR, "Failed to start loop: %d", status);
    Shutdown();
    return status;
  }

  // TODO(48099): Determine if a configuration blob is needed, and if so, send it here.
  return ZX_OK;
}

zx::status<uint8_t> Gt6853Device::ReadReg8(const Register reg) {
  const uint16_t address = htobe16(static_cast<uint16_t>(reg));
  uint8_t value = 0;
  zx_status_t status = i2c_.WriteReadSync(reinterpret_cast<const uint8_t*>(&address),
                                          sizeof(address), &value, sizeof(value));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from 0x%02x: %d", address, status);
    return zx::error_status(status);
  }

  return zx::ok(value);
}

zx::status<> Gt6853Device::Read(const Register reg, uint8_t* const buffer, const size_t size) {
  const uint16_t address = htobe16(static_cast<uint16_t>(reg));
  zx_status_t status =
      i2c_.WriteReadSync(reinterpret_cast<const uint8_t*>(&address), sizeof(address), buffer, size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read %zu bytes from 0x%02x: %d", size, address, status);
    return zx::error_status(status);
  }

  return zx::ok();
}

zx::status<> Gt6853Device::WriteReg8(const Register reg, const uint8_t value) {
  const uint16_t address = static_cast<uint16_t>(reg);
  const uint8_t buffer[] = {
      static_cast<uint8_t>(address >> 8),
      static_cast<uint8_t>(address & 0xff),
      value,
  };
  zx_status_t status = i2c_.WriteSync(buffer, sizeof(buffer));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write 0x%02x to 0x%02x: %d", value, address, status);
    return zx::error_status(status);
  }
  return zx::ok();
}

int Gt6853Device::Thread() {
  zx::time timestamp;
  while (interrupt_.wait(&timestamp) == ZX_OK) {
    zx::status<uint8_t> status = ReadReg8(Register::kEventStatusReg);
    if (status.is_error()) {
      zxlogf(ERROR, "Failed to read event status register");
      return thrd_error;
    }
    if (status.value() != kTouchEvent) {
      continue;
    }

    if ((status = ReadReg8(Register::kContactsReg)).is_error()) {
      zxlogf(ERROR, "Failed to read contact count register");
      return thrd_error;
    }

    const uint8_t contacts = status.value() & 0b1111;
    if (contacts > kMaxContacts) {
      zxlogf(ERROR, "Touch event with too many contacts: %u", contacts);
      return thrd_error;
    }

    uint8_t contacts_buffer[kContactSize * kMaxContacts] = {};
    if (Read(Register::kContactsStartReg, contacts_buffer, contacts * kContactSize).is_error()) {
      zxlogf(ERROR, "Failed to read contacts");
      return thrd_error;
    }

    // Clear the status register so that interrupts stop being generated.
    if (WriteReg8(Register::kEventStatusReg, 0).is_error()) {
      zxlogf(ERROR, "Failed to reset event status register");
      return thrd_error;
    }

    Gt6853InputReport report = {
        .event_time = timestamp,
        .contacts = {},
        .num_contacts = contacts,
    };
    for (uint8_t i = 0; i < contacts; i++) {
      report.contacts[i] = ParseContact(&contacts_buffer[i * kContactSize]);
    }

    input_report_readers_.SendReportToAllReaders(report);
  }

  return thrd_success;
}

void Gt6853Device::Shutdown() {
  interrupt_.destroy();
  thrd_join(thread_, nullptr);
}

static zx_driver_ops_t gt6853_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Gt6853Device::Create;
  return ops;
}();

}  // namespace touch

ZIRCON_DRIVER(Gt6853Device, touch::gt6853_driver_ops, "zircon", "0.1")
