// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ft8201.h"

#include <lib/zx/profile.h>
#include <threads.h>
#include <zircon/threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/composite.h>
#include <fbl/auto_lock.h>

namespace {

enum {
  kFragmentI2c,
  kFragmentInterruptGpio,
  kFragmentResetGpio,
  kFragmentCount,
};

// TODO(bradenkell): Double-check these values.
constexpr int64_t kMaxContactX = 1279;
constexpr int64_t kMaxContactY = 799;

}  // namespace

namespace touch {

fuchsia_input_report::InputReport Ft8201InputReport::ToFidlInputReport(fidl::Allocator& allocator) {
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

  return fuchsia_input_report::InputReport::Builder(
             allocator.make<fuchsia_input_report::InputReport::Frame>())
      .set_event_time(std::move(time))
      .set_touch(allocator.make<fuchsia_input_report::TouchInputReport>(touch_report.build()))
      .build();
}

std::unique_ptr<Ft8201InputReportsReader> Ft8201InputReportsReader::Create(
    Ft8201Device* const base, async_dispatcher_t* dispatcher, zx::channel server) {
  fidl::OnUnboundFn<fuchsia_input_report::InputReportsReader::Interface> unbound_fn(
      [](fuchsia_input_report::InputReportsReader::Interface* dev, fidl::UnbindInfo info,
         zx::channel channel) {
        auto* device = static_cast<Ft8201InputReportsReader*>(dev);

        {
          fbl::AutoLock lock(&device->report_lock_);
          if (device->completer_) {
            device->completer_.reset();
          }
        }

        device->base_->RemoveReaderFromList(device);
      });

  auto reader = std::make_unique<Ft8201InputReportsReader>(base);

  auto binding = fidl::BindServer(
      dispatcher, std::move(server),
      static_cast<fuchsia_input_report::InputReportsReader::Interface*>(reader.get()),
      std::move(unbound_fn));
  if (binding.is_error()) {
    zxlogf(ERROR, "Ft8201: BindServer failed: %d\n", binding.error());
    return nullptr;
  }

  return reader;
}

void Ft8201InputReportsReader::ReceiveReport(const Ft8201InputReport& report) {
  fbl::AutoLock lock(&report_lock_);
  if (reports_data_.full()) {
    reports_data_.pop();
  }

  reports_data_.push(report);

  if (completer_) {
    ReplyWithReports(*completer_);
    completer_.reset();
  }
}

void Ft8201InputReportsReader::ReadInputReports(ReadInputReportsCompleter::Sync completer) {
  fbl::AutoLock lock(&report_lock_);
  if (completer_) {
    completer.ReplyError(ZX_ERR_ALREADY_BOUND);
  } else if (reports_data_.empty()) {
    completer_.emplace(completer.ToAsync());
  } else {
    ReplyWithReports(completer);
  }
}

void Ft8201InputReportsReader::ReplyWithReports(ReadInputReportsCompleterBase& completer) {
  std::array<fuchsia_input_report::InputReport, fuchsia_input_report::MAX_DEVICE_REPORT_COUNT>
      reports;

  size_t num_reports = 0;
  for (; !reports_data_.empty() && num_reports < reports.size(); num_reports++) {
    reports[num_reports] = reports_data_.front().ToFidlInputReport(report_allocator_);
    reports_data_.pop();
  }

  completer.ReplySuccess(fidl::VectorView(fidl::unowned_ptr(reports.data()), num_reports));

  if (reports_data_.empty()) {
    report_allocator_.inner_allocator().reset();
  }
}

zx::status<Ft8201Device*> Ft8201Device::CreateAndGetDevice(void* ctx, zx_device_t* parent) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "Ft8201: Failed to get composite protocol");
    return zx::error(ZX_ERR_NO_RESOURCES);
  }

  zx_device_t* fragments[kFragmentCount] = {};
  size_t fragment_count = 0;
  composite.GetFragments(fragments, std::size(fragments), &fragment_count);
  if (fragment_count != std::size(fragments)) {
    zxlogf(ERROR, "Ft8201: Received %zu fragments, expected %zu", fragment_count,
           std::size(fragments));
    return zx::error(ZX_ERR_NO_RESOURCES);
  }

  ddk::I2cChannel i2c(fragments[kFragmentI2c]);
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "Ft8201: Failed to get I2C fragment");
    return zx::error(ZX_ERR_NO_RESOURCES);
  }

  ddk::GpioProtocolClient interrupt_gpio(fragments[kFragmentInterruptGpio]);
  if (!interrupt_gpio.is_valid()) {
    zxlogf(ERROR, "Ft8201: Failed to get interrupt GPIO fragment");
    return zx::error(ZX_ERR_NO_RESOURCES);
  }

  ddk::GpioProtocolClient reset_gpio(fragments[kFragmentResetGpio]);
  if (!reset_gpio.is_valid()) {
    zxlogf(ERROR, "Ft8201: Failed to get reset GPIO fragment");
    return zx::error(ZX_ERR_NO_RESOURCES);
  }

  std::unique_ptr<Ft8201Device> device =
      std::make_unique<Ft8201Device>(parent, i2c, interrupt_gpio, reset_gpio);
  zx_status_t status = device->Init();
  if (status != ZX_OK) {
    return zx::error(status);
  }

  if ((status = device->DdkAdd("ft8201")) != ZX_OK) {
    zxlogf(ERROR, "Ft8201: DdkAdd failed: %d", status);
    return zx::error(status);
  }

  return zx::ok(device.release());
}

zx_status_t Ft8201Device::Create(void* ctx, zx_device_t* parent) {
  auto status = CreateAndGetDevice(ctx, parent);
  return status.is_error() ? status.error_value() : ZX_OK;
}

zx_status_t Ft8201Device::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fuchsia_input_report::InputDevice::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void Ft8201Device::DdkUnbindNew(ddk::UnbindTxn txn) {
  Shutdown();
  txn.Reply();
}

void Ft8201Device::GetInputReportsReader(zx::channel server,
                                         GetInputReportsReaderCompleter::Sync completer) {
  fbl::AutoLock lock(&readers_lock_);
  auto reader = Ft8201InputReportsReader::Create(this, loop_.dispatcher(), std::move(server));
  if (reader) {
    readers_list_.push_back(std::move(reader));
    sync_completion_signal(&next_reader_wait_);  // Only for tests.
  }
}

void Ft8201Device::GetDescriptor(GetDescriptorCompleter::Sync completer) {
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
      allocator.make<fuchsia_input_report::ContactInputDescriptor[]>(kNumContacts);
  for (uint32_t i = 0; i < kNumContacts; i++) {
    touch_input_contacts[i] =
        fuchsia_input_report::ContactInputDescriptor::Builder(
            allocator.make<fuchsia_input_report::ContactInputDescriptor::Frame>())
            .set_position_x(allocator.make<fuchsia_input_report::Axis>(kAxisX))
            .set_position_y(allocator.make<fuchsia_input_report::Axis>(kAxisY))
            .build();
  }

  // TODO(bradenkell): Find out if 8201 supports reporting contact dimensions, pressure.

  auto touch_input_descriptor =
      fuchsia_input_report::TouchInputDescriptor::Builder(
          allocator.make<fuchsia_input_report::TouchInputDescriptor::Frame>())
          .set_contacts(
              allocator.make<fidl::VectorView<fuchsia_input_report::ContactInputDescriptor>>(
                  std::move(touch_input_contacts), kNumContacts))
          .set_max_contacts(allocator.make<uint32_t>(kNumContacts))
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

void Ft8201Device::SendOutputReport(fuchsia_input_report::OutputReport report,
                                    SendOutputReportCompleter::Sync completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Ft8201Device::GetFeatureReport(GetFeatureReportCompleter::Sync completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Ft8201Device::SetFeatureReport(fuchsia_input_report::FeatureReport report,
                                    SetFeatureReportCompleter::Sync completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Ft8201Device::RemoveReaderFromList(Ft8201InputReportsReader* reader) {
  fbl::AutoLock lock(&readers_lock_);
  for (auto iter = readers_list_.begin(); iter != readers_list_.end(); ++iter) {
    if (iter->get() == reader) {
      readers_list_.erase(iter);
      break;
    }
  }
}

void Ft8201Device::WaitForNextReader() {
  sync_completion_wait(&next_reader_wait_, ZX_TIME_INFINITE);
  sync_completion_reset(&next_reader_wait_);
}

zx_status_t Ft8201Device::Init() {
  zx_status_t status = interrupt_gpio_.ConfigIn(GPIO_NO_PULL);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Ft8201: ConfigIn failed: %d", status);
    return status;
  }

  if ((status = interrupt_gpio_.GetInterrupt(ZX_INTERRUPT_MODE_EDGE_LOW, &interrupt_)) != ZX_OK) {
    zxlogf(ERROR, "Ft8201: GetInterrupt failed: %d", status);
    return status;
  }

  status = thrd_create_with_name(
      &thread_, [](void* arg) -> int { return reinterpret_cast<Ft8201Device*>(arg)->Thread(); },
      this, "ft8201-thread");
  if (status != thrd_success) {
    zxlogf(ERROR, "Ft8201: Failed to create thread: %d", status);
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
                                         "ft8201-thread", profile.reset_and_get_address());
    if (status != ZX_OK) {
      zxlogf(WARNING, "Ft8201: Failed to get deadline profile: %d", status);
    } else {
      status = zx_object_set_profile(thrd_get_zx_handle(thread_), profile.get(), 0);
      if (status != ZX_OK) {
        zxlogf(WARNING, "Ft8201: Failed to apply deadline profile to device thread: %d", status);
      }
    }
  }

  if ((status = loop_.StartThread("ft8201-reader-thread")) != ZX_OK) {
    zxlogf(ERROR, "Ft8201: Failed to start loop: %d", status);
    Shutdown();
    return status;
  }

  return ZX_OK;
}

int Ft8201Device::Thread() {
  zx::time timestamp;
  while (interrupt_.wait(&timestamp) == ZX_OK) {
    // TODO(bradenkell): Populate the contacts.
    Ft8201InputReport report = {.event_time = timestamp};

    fbl::AutoLock lock(&readers_lock_);
    for (auto& reader : readers_list_) {
      reader->ReceiveReport(report);
    }
  }

  return thrd_success;
}

void Ft8201Device::Shutdown() {
  interrupt_.destroy();
  thrd_join(thread_, nullptr);
}

static zx_driver_ops_t ft8201_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Ft8201Device::Create;
  return ops;
}();

}  // namespace touch

// clang-format off
ZIRCON_DRIVER_BEGIN(Ft8201Device, touch::ft8201_driver_ops, "zircon", "0.1", 3)
  BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_FOCALTECH),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_FOCALTECH_FT8201),
ZIRCON_DRIVER_END(Ft8201Device)
