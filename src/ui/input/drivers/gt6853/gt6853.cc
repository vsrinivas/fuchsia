// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gt6853.h"

#include <endian.h>
#include <lib/zx/profile.h>
#include <threads.h>
#include <zircon/threads.h>

#include <ddk/debug.h>
#include <ddk/metadata.h>
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

constexpr uint8_t kCpuCtrlHoldSs51 = 0x24;

constexpr zx::duration kResetSetupTime = zx::msec(2);

constexpr int kFirmwareTries = 200;

constexpr size_t kMaxSubsysCount = 28;
constexpr size_t kI2cMaxTransferSize = 256;

constexpr uint8_t kCpuRunFromFlash[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr uint8_t kCpuRunFromRam[8] = {0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};

}  // namespace

namespace touch {

enum class Gt6853Device::HostCommand : uint8_t {
  kConfigStart = 0x80,
  kConfigEnd = 0x83,
};

enum class Gt6853Device::DeviceCommand : uint8_t {
  kReadyForConfig = 0x82,
  kDeviceIdle = 0xff,
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

  if ((status = UpdateFirmwareIfNeeded()) != ZX_OK) {
    return status;
  }

  if ((status = DownloadConfigIfNeeded()) != ZX_OK) {
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

  return ZX_OK;
}

zx_status_t Gt6853Device::DownloadConfigIfNeeded() {
  zx::vmo config_vmo;

  bool use_9365_config = false;
  size_t actual = 0;
  zx_status_t status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, &use_9365_config,
                                           sizeof(use_9365_config), &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to get device metadata: %d", status);
    return status;
  }
  if (actual != sizeof(use_9365_config)) {
    zxlogf(ERROR, "Expected metadata size %zu, got %zu", sizeof(use_9365_config), actual);
    return ZX_ERR_BAD_STATE;
  }

  size_t config_vmo_size = 0;
  if (use_9365_config) {
    status = load_firmware(parent(), GT6853_CONFIG_9365_PATH, config_vmo.reset_and_get_address(),
                           &config_vmo_size);
  } else {
    status = load_firmware(parent(), GT6853_CONFIG_9364_PATH, config_vmo.reset_and_get_address(),
                           &config_vmo_size);
  }

  if (status != ZX_OK) {
    zxlogf(WARNING, "Failed to load config binary, skipping config download");
    return ZX_OK;
  }

  zx::status<uint8_t> sensor_id = ReadReg8(Register::kSensorIdReg);
  if (sensor_id.is_error()) {
    zxlogf(ERROR, "Failed to read sensor ID register: %d", sensor_id.error_value());
    return sensor_id.error_value();
  }

  zxlogf(INFO, "Sensor ID 0x%02x, using 936%d config", sensor_id.value(), use_9365_config ? 5 : 4);

  zx::status<uint64_t> config_offset =
      GetConfigOffset(config_vmo, config_vmo_size, sensor_id.value() & 0xf);
  if (config_offset.is_error()) {
    return config_offset.error_value();
  }

  uint32_t config_size = 0;
  if (config_vmo_size < config_offset.value() + sizeof(config_size)) {
    zxlogf(ERROR, "Config VMO size is %zu, must be at least %lu", config_vmo_size,
           config_offset.value() + sizeof(config_size));
    return ZX_ERR_IO_INVALID;
  }

  status = config_vmo.read(&config_size, config_offset.value(), sizeof(config_size));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read config VMO: %d", status);
    return status;
  }

  // The offset of the config data in each config table entry.
  constexpr uint32_t kConfigDataOffset = 121;

  config_size = le32toh(config_size);
  if (config_size < kConfigDataOffset) {
    zxlogf(ERROR, "Config size is %u, must be at least %u", config_size, kConfigDataOffset);
    return ZX_ERR_IO_INVALID;
  }

  zxlogf(INFO, "Found %u-byte config at offset %lu", config_size, config_offset.value());

  return SendConfig(config_vmo, config_offset.value() + kConfigDataOffset,
                    config_size - kConfigDataOffset);
}

zx::status<uint64_t> Gt6853Device::GetConfigOffset(const zx::vmo& config_vmo,
                                                   const size_t config_vmo_size,
                                                   const uint8_t sensor_id) {
  constexpr size_t kConfigTableHeaderSize = 16;
  if (config_vmo_size < kConfigTableHeaderSize) {
    zxlogf(ERROR, "Config VMO size is %zu, must be at least %zu", config_vmo_size,
           kConfigTableHeaderSize);
    return zx::error(ZX_ERR_IO_INVALID);
  }

  uint32_t config_size = 0;
  zx_status_t status = config_vmo.read(&config_size, 0, sizeof(config_size));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read config VMO: %d", status);
    return zx::error(status);
  }

  config_size = le32toh(config_size);
  if (config_size != config_vmo_size) {
    zxlogf(ERROR, "Config size (%u) doesnt't match VMO size (%zu)", config_size, config_vmo_size);
    return zx::error(ZX_ERR_IO_INVALID);
  }

  // TODO(bradenkell): Check config CRC byte.

  // The offset of the config entry count in the table header.
  constexpr uint64_t kConfigEntryCountOffset = 9;

  uint8_t config_count = 0;
  status = config_vmo.read(&config_count, kConfigEntryCountOffset, sizeof(config_count));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read config VMO: %d", status);
    return zx::error(status);
  }

  if (config_vmo_size < (kConfigTableHeaderSize + (config_count * sizeof(uint16_t)))) {
    zxlogf(ERROR, "Config VMO size is %zu, must be at least %zu", config_vmo_size,
           kConfigTableHeaderSize + (config_count * sizeof(uint16_t)));
    return zx::error(ZX_ERR_IO_INVALID);
  }

  for (int i = 0; i < config_count; i++) {
    const uint64_t config_offset_offset = kConfigTableHeaderSize + (i * sizeof(uint16_t));
    uint16_t config_offset = 0;
    status = config_vmo.read(&config_offset, config_offset_offset, sizeof(config_offset));
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to read config VMO: %d", status);
      return zx::error(status);
    }

    // The offset of the sensor ID in each config table entry.
    constexpr uint64_t kConfigSensorIdOffset = 20;

    config_offset = le16toh(config_offset);
    if (config_vmo_size < config_offset + kConfigSensorIdOffset) {
      zxlogf(ERROR, "Config offset %u is too big", config_offset);
      return zx::error(ZX_ERR_IO_INVALID);
    }

    uint8_t config_sensor_id = 0;
    status = config_vmo.read(&config_sensor_id, config_offset + kConfigSensorIdOffset,
                             sizeof(config_sensor_id));
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to read config VMO: %d", status);
      return zx::error(status);
    }

    if (config_sensor_id == sensor_id) {
      return zx::ok(config_offset);
    }
  }

  zxlogf(ERROR, "Failed to find config for sensor ID 0x%02x", sensor_id);
  return zx::error(ZX_ERR_NOT_FOUND);
}

zx_status_t Gt6853Device::PollCommandRegister(const DeviceCommand command) {
  constexpr int kCommandTimeoutMs = 100;  // An arbitrary timeout that seems to work.
  for (int i = 0; i < kCommandTimeoutMs; i++) {
    auto status = ReadReg8(Register::kCommandReg);
    if (status.is_error()) {
      zxlogf(ERROR, "Failed to read command register");
      return status.error_value();
    }

    if (status.value() == static_cast<uint8_t>(command)) {
      return ZX_OK;
    }

    zx::nanosleep(zx::deadline_after(zx::msec(1)));
  }

  zxlogf(ERROR, "Timed out waiting for command register 0x%02x", static_cast<uint8_t>(command));
  return ZX_ERR_TIMED_OUT;
}

zx_status_t Gt6853Device::SendCommand(const HostCommand command) {
  const uint8_t checksum = 0xff - static_cast<uint8_t>(command) + 1;
  uint8_t buffer[] = {
      static_cast<uint16_t>(Register::kCommandReg) >> 8,
      static_cast<uint16_t>(Register::kCommandReg) & 0xff,
      static_cast<uint8_t>(command),
      0x00,
      checksum,
  };
  zx_status_t status = i2c_.WriteSync(buffer, sizeof(buffer));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to send command 0x%02x: %d", static_cast<uint8_t>(command), status);
    return status;
  }
  return ZX_OK;
}

zx_status_t Gt6853Device::SendConfig(const zx::vmo& config_vmo, uint64_t offset, size_t size) {
  zx_status_t status = PollCommandRegister(DeviceCommand::kDeviceIdle);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Device not idle before config download");
    return status;
  }

  if ((status = SendCommand(HostCommand::kConfigStart)) != ZX_OK) {
    zxlogf(ERROR, "Failed to start config download");
    return status;
  }

  if ((status = PollCommandRegister(DeviceCommand::kReadyForConfig)) != ZX_OK) {
    return status;
  }

  constexpr size_t kMaxConfigPacketSize = 128;

  while (size > 0) {
    const size_t tx_size = std::min(size, kMaxConfigPacketSize);
    uint8_t buffer[sizeof(Register::kConfigDataReg) + kMaxConfigPacketSize];

    status = config_vmo.read(&buffer[sizeof(Register::kConfigDataReg)], offset, tx_size);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to read config VMO: %d", status);
      return status;
    }

    buffer[0] = static_cast<uint16_t>(Register::kConfigDataReg) >> 8;
    buffer[1] = static_cast<uint16_t>(Register::kConfigDataReg) & 0xff;
    if ((status = i2c_.WriteSync(buffer, tx_size + sizeof(Register::kConfigDataReg))) != ZX_OK) {
      zxlogf(ERROR, "Failed to write %zu config bytes: %d", tx_size, status);
      return status;
    }

    size -= tx_size;
    offset += tx_size;
  }

  if ((status = SendCommand(HostCommand::kConfigEnd)) != ZX_OK) {
    zxlogf(ERROR, "Failed to stop config download");
    return status;
  }

  if ((status = PollCommandRegister(DeviceCommand::kDeviceIdle)) != ZX_OK) {
    zxlogf(ERROR, "Device not idle after config download");
    return status;
  }

  return ZX_OK;
}

zx_status_t Gt6853Device::UpdateFirmwareIfNeeded() {
  zx::vmo fw_vmo;
  size_t fw_vmo_size = 0;
  zx_status_t status =
      load_firmware(parent(), GT6853_FIRMWARE_PATH, fw_vmo.reset_and_get_address(), &fw_vmo_size);
  if (status != ZX_OK) {
    zxlogf(WARNING, "Failed to load firmware binary, skipping firmware update");
    return ZX_OK;
  }

  fzl::VmoMapper mapped_fw;
  if ((status = mapped_fw.Map(fw_vmo, 0, fw_vmo_size, ZX_VM_PERM_READ)) != ZX_OK) {
    zxlogf(ERROR, "Failed to map firmware VMO: %d", status);
    return status;
  }

  FirmwareSubsysInfo subsys_entries[kMaxSubsysCount];
  zx::status<size_t> entry_count = ParseFirmwareInfo(mapped_fw, subsys_entries);
  if (entry_count.is_error()) {
    return entry_count.error_value();
  }

  fbl::Span<const FirmwareSubsysInfo> subsys_span(subsys_entries, entry_count.value());
  if ((status = PrepareFirmwareUpdate(subsys_span)) != ZX_OK) {
    return status;
  }

  for (const FirmwareSubsysInfo& subsys_info : subsys_span.subspan(1)) {
    if ((status = FlashSubsystem(subsys_info)) != ZX_OK) {
      return status;
    }
  }

  return FinishFirmwareUpdate();
}

zx::status<size_t> Gt6853Device::ParseFirmwareInfo(const fzl::VmoMapper& mapped_fw,
                                                   FirmwareSubsysInfo* out_subsys_entries) {
  constexpr size_t kFirmwareHeaderSize = 32;
  constexpr size_t kSubsysCountOffset = 27;
  constexpr size_t kSubsysEntrySize = 8;
  constexpr size_t kSubsysDataOffset = kFirmwareHeaderSize + (kMaxSubsysCount * kSubsysEntrySize);

  if (mapped_fw.size() < kSubsysDataOffset) {
    zxlogf(ERROR, "Firmware VMO size is %zu, must be at least %zu", mapped_fw.size(),
           kSubsysDataOffset);
    return zx::error(ZX_ERR_IO_INVALID);
  }

  const uint8_t* fw_data = static_cast<const uint8_t*>(mapped_fw.start());

  uint32_t fw_size;
  memcpy(&fw_size, fw_data, sizeof(fw_size));
  fw_size = be32toh(fw_size);

  uint16_t checksum;

  if (fw_size + sizeof(fw_size) + sizeof(checksum) != mapped_fw.size()) {
    zxlogf(ERROR, "Firmware header indicates size %zu, but VMO size is %zu",
           fw_size + sizeof(fw_size) + sizeof(checksum), mapped_fw.size());
    return zx::error(ZX_ERR_IO_INVALID);
  }

  memcpy(&checksum, fw_data + sizeof(fw_size), sizeof(checksum));
  checksum = be16toh(checksum);

  uint16_t expected_checksum = 0;
  for (size_t i = sizeof(fw_size) + sizeof(checksum); i < mapped_fw.size(); i++) {
    expected_checksum += fw_data[i];
  }

  if (checksum != expected_checksum) {
    zxlogf(ERROR, "Firmware checksum doesn't match calculated value");
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }

  const uint8_t subsys_count = fw_data[kSubsysCountOffset];
  if (subsys_count > kMaxSubsysCount) {
    zxlogf(ERROR, "Firmware subsys count is %u, only %zu are allowed", subsys_count,
           kMaxSubsysCount);
    return zx::error(ZX_ERR_IO_INVALID);
  }

  size_t subsys_data_offset = kSubsysDataOffset;
  for (uint32_t i = 0; i < subsys_count; i++) {
    const uint8_t* subsys_header = fw_data + kFirmwareHeaderSize + (i * kSubsysEntrySize);
    FirmwareSubsysInfo& entry = out_subsys_entries[i];

    entry.type = *subsys_header++;

    memcpy(&entry.size, subsys_header, sizeof(entry.size));
    entry.size = be32toh(entry.size);
    subsys_header += sizeof(entry.size);

    memcpy(&entry.flash_addr, subsys_header, sizeof(entry.flash_addr));
    entry.flash_addr = be16toh(entry.flash_addr);

    entry.data = fw_data + subsys_data_offset;
    subsys_data_offset += entry.size;

    if (subsys_data_offset > mapped_fw.size()) {
      zxlogf(ERROR, "Subsys offset %zu exceeds firmware size", subsys_data_offset);
      return zx::error(ZX_ERR_IO_INVALID);
    }
  }

  return zx::ok(subsys_count);
}

zx_status_t Gt6853Device::PrepareFirmwareUpdate(
    fbl::Span<const FirmwareSubsysInfo> subsys_entries) {
  constexpr zx::duration kResetHoldTime = zx::msec(10);
  constexpr int kHoldSs51Tries = 20;
  constexpr zx::duration kHoldSs51TryInterval = zx::msec(20);

  if (subsys_entries.empty()) {
    zxlogf(ERROR, "Expected at least one firmware subsys entry");
    return ZX_ERR_IO_INVALID;
  }

  zx::status<> status = Write(Register::kCpuRunFrom, kCpuRunFromFlash, sizeof(kCpuRunFromFlash));
  if (status.is_error()) {
    return status.error_value();
  }

  reset_gpio_.ConfigOut(0);
  zx::nanosleep(zx::deadline_after(kResetSetupTime));
  reset_gpio_.Write(1);
  zx::nanosleep(zx::deadline_after(kResetHoldTime));

  for (int i = 0; i < kHoldSs51Tries; i++) {
    status = WriteAndCheck(Register::kCpuCtrl, &kCpuCtrlHoldSs51, sizeof(kCpuCtrlHoldSs51));
    if (status.is_error()) {
      zx::nanosleep(zx::deadline_after(kHoldSs51TryInterval));
    } else {
      break;
    }
  }
  if (status.is_error()) {
    zxlogf(ERROR, "Timed out waiting for CPU control register");
    return ZX_ERR_TIMED_OUT;
  }

  const uint8_t dsp_mcu_power = 0;
  status = WriteAndCheck(Register::kDspMcuPower, &dsp_mcu_power, sizeof(dsp_mcu_power));
  if (status.is_error()) {
    zxlogf(ERROR, "Failed to enable DSP/MCU power: %d", status.error_value());
    return status.error_value();
  }

  // Disable the watchdog timer.
  constexpr uint8_t kWatchdogDisableKey1 = 0x95;
  constexpr uint8_t kWatchdogDisableKey2 = 0x27;

  if ((status = WriteReg8(Register::kCache, 0)).is_error()) {
    return status.error_value();
  }
  if ((status = WriteReg8(Register::kEsdKey, kWatchdogDisableKey1)).is_error()) {
    return status.error_value();
  }
  if ((status = WriteReg8(Register::kWtdTimer, 0)).is_error()) {
    return status.error_value();
  }
  if ((status = WriteReg8(Register::kEsdKey, kWatchdogDisableKey2)).is_error()) {
    return status.error_value();
  }

  if ((status = WriteReg8(Register::kScramble, 0)).is_error()) {
    return status.error_value();
  }

  return LoadIsp(subsys_entries[0]);
}

zx_status_t Gt6853Device::LoadIsp(const FirmwareSubsysInfo& isp_info) {
  zx::status status = WriteReg8(Register::kBankSelect, 0);
  if (status.is_error()) {
    return status.error_value();
  }

  if ((status = WriteReg8(Register::kAccessPatch0, 1)).is_error()) {
    return status.error_value();
  }

  if ((status = WriteAndCheck(Register::kIspAddr, isp_info.data, isp_info.size)).is_error()) {
    zxlogf(ERROR, "Failed to write ISP data: %d", status.error_value());
    return status.error_value();
  }

  const uint8_t disable_patch0_access = 0;
  if ((status = WriteAndCheck(Register::kAccessPatch0, &disable_patch0_access, 1)).is_error()) {
    zxlogf(ERROR, "Failed to write disable patch0 access: %d", status.error_value());
    return status.error_value();
  }

  const uint8_t isp_run_flag[2] = {0, 0};
  if ((status = Write(Register::kIspRunFlag, isp_run_flag, sizeof(isp_run_flag))).is_error()) {
    return status.error_value();
  }

  if ((status = Write(Register::kCpuRunFrom, kCpuRunFromRam, sizeof(kCpuRunFromRam))).is_error()) {
    return status.error_value();
  }

  if ((status = WriteReg8(Register::kCpuCtrl, 0)).is_error()) {
    return status.error_value();
  }

  constexpr uint8_t kIspRunFlagWorking1 = 0xaa;
  constexpr uint8_t kIspRunFlagWorking2 = 0xbb;
  constexpr zx::duration kIspRunFlagTryInterval = zx::msec(10);

  for (int i = 0; i < kFirmwareTries; i++) {
    zx::nanosleep(zx::deadline_after(kIspRunFlagTryInterval));
    uint8_t isp_run_check[2];
    if (Read(Register::kIspRunFlag, isp_run_check, sizeof(isp_run_check)).is_ok()) {
      if (isp_run_check[0] == kIspRunFlagWorking1 && isp_run_check[1] == kIspRunFlagWorking2) {
        return ZX_OK;
      }
    }
  }

  zxlogf(ERROR, "Timed out waiting for ISP to be ready");
  return ZX_ERR_TIMED_OUT;
}

zx_status_t Gt6853Device::FlashSubsystem(const FirmwareSubsysInfo& subsys_info) {
  constexpr size_t kIspMaxTransferSize = 1024 * 4;
  constexpr size_t kPacketHeaderAndChecksumSize = sizeof(uint16_t) * 3;
  constexpr int kFirmwarePacketTries = 3;

  // Packet format (total size n, all fields big-endian):
  // 0x00: data length
  // 0x02: flash address
  // 0x04: data
  //  ...: data
  //  n-2: checksum of data length, flash address, and data fields

  size_t remaining = subsys_info.size;
  uint32_t offset = 0;
  while (remaining > 0) {
    const uint16_t transfer_size = std::min(remaining, kIspMaxTransferSize);

    uint8_t packet_buffer[kIspMaxTransferSize + kPacketHeaderAndChecksumSize];

    const uint16_t transfer_size_be = htobe16(transfer_size);
    memcpy(packet_buffer, &transfer_size_be, 2);

    const uint16_t flash_addr_be = htobe16(subsys_info.flash_addr + (offset >> 8));
    memcpy(packet_buffer + 2, &flash_addr_be, 2);

    memcpy(packet_buffer + 4, subsys_info.data + offset, transfer_size);

    const uint16_t checksum_be = htobe16(Checksum16(packet_buffer, transfer_size + 4));
    memcpy(packet_buffer + transfer_size + 4, &checksum_be, 2);

    zx_status_t status;
    for (int i = 0; i < kFirmwarePacketTries; i++) {
      status = SendFirmwarePacket(subsys_info.type, packet_buffer,
                                  transfer_size + kPacketHeaderAndChecksumSize);
      if (status == ZX_OK) {
        break;
      }
    }
    if (status != ZX_OK) {
      zxlogf(ERROR, "Exhausted retries for sending subsys %u packet", subsys_info.type);
      return status;
    }

    offset += transfer_size;
    remaining -= transfer_size;
  }

  return ZX_OK;
}

uint16_t Gt6853Device::Checksum16(const uint8_t* data, const size_t size) {
  ZX_ASSERT(size % 2 == 0);

  uint16_t checksum = 0;
  for (size_t i = 0; i < size; i += 2) {
    uint16_t entry;
    memcpy(&entry, data + i, 2);
    checksum += be16toh(entry);
  }

  return ~checksum + 1;
}

zx_status_t Gt6853Device::SendFirmwarePacket(const uint8_t type, const uint8_t* packet,
                                             const size_t size) {
  constexpr uint8_t kFlashStatusWriting = 0xaa;
  constexpr uint8_t kFlashStatusSuccess = 0xbb;
  constexpr uint8_t kFlashStatusError = 0xcc;
  constexpr uint8_t kFlashStatusCheckError = 0xdd;
  constexpr zx::duration kFlashWritingWait = zx::msec(55);

  zx::status<> status = WriteAndCheck(Register::kIspBuffer, packet, size);
  if (status.is_error()) {
    zxlogf(ERROR, "Failed to send firmware packet: %d", status.error_value());
    return status.error_value();
  }

  const uint8_t flash_flag[2] = {0, 0};
  if ((status = WriteAndCheck(Register::kFlashFlag, flash_flag, sizeof(flash_flag))).is_error()) {
    zxlogf(ERROR, "Failed to set flash flag: %d", status.error_value());
    return status.error_value();
  }

  const uint8_t subsys_type[2] = {type, type};
  status = WriteAndCheck(Register::kSubsysType, subsys_type, sizeof(subsys_type));
  if (status.is_error()) {
    zxlogf(ERROR, "Failed to set subsys type to %u: %d", type, status.error_value());
    return status.error_value();
  }

  for (int i = 0; i < kFirmwareTries; i++) {
    uint8_t flash_status[2];
    if ((status = Read(Register::kFlashFlag, flash_status, sizeof(flash_status))).is_error()) {
      return status.error_value();
    }

    if (flash_status[0] == kFlashStatusWriting && flash_status[1] == kFlashStatusWriting) {
      zx::nanosleep(zx::deadline_after(kFlashWritingWait));
      continue;
    }

    if (flash_status[0] == kFlashStatusSuccess && flash_status[1] == kFlashStatusSuccess) {
      if ((status = Read(Register::kFlashFlag, flash_status, sizeof(flash_status))).is_error()) {
        return status.error_value();
      }
      if (flash_status[0] == kFlashStatusSuccess && flash_status[1] == kFlashStatusSuccess) {
        return ZX_OK;
      }
    }

    if (flash_status[0] == kFlashStatusError && flash_status[1] == kFlashStatusError) {
      zxlogf(ERROR, "Failed to flash subsys %u", type);
      return ZX_ERR_IO;
    }

    if (flash_status[0] == kFlashStatusCheckError) {
      zxlogf(ERROR, "Flash checksum error for subsys %u", type);
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    zx::nanosleep(zx::deadline_after(zx::msec(1)));
  }

  zxlogf(ERROR, "Timed out waiting for subsys %u flash to complete", type);
  return ZX_ERR_TIMED_OUT;
}

zx_status_t Gt6853Device::FinishFirmwareUpdate() {
  constexpr zx::duration kResetHoldTime = zx::msec(80);

  zx::status<> status = WriteReg8(Register::kCpuCtrl, kCpuCtrlHoldSs51);
  if (status.is_error()) {
    return status.error_value();
  }

  status = Write(Register::kCpuRunFrom, kCpuRunFromFlash, sizeof(kCpuRunFromFlash));
  if (status.is_error()) {
    return status.error_value();
  }

  if ((status = WriteReg8(Register::kCpuCtrl, 0)).is_error()) {
    return status.error_value();
  }

  reset_gpio_.Write(0);
  zx::nanosleep(zx::deadline_after(kResetSetupTime));
  reset_gpio_.Write(1);
  zx::nanosleep(zx::deadline_after(kResetHoldTime));

  zxlogf(INFO, "Updated firmware, reset IC");
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

zx::status<> Gt6853Device::Read(const Register reg, uint8_t* buffer, const size_t size) {
  uint16_t address = static_cast<uint16_t>(reg);
  size_t remaining = size;

  while (remaining > 0) {
    const size_t transfer_size = std::min(remaining, kI2cMaxTransferSize);
    const uint16_t be_address = htobe16(address);
    zx_status_t status = i2c_.WriteReadSync(reinterpret_cast<const uint8_t*>(&be_address),
                                            sizeof(be_address), buffer, transfer_size);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to read %zu bytes from 0x%02x: %d", transfer_size, address, status);
      return zx::error_status(status);
    }

    address += transfer_size;
    buffer += transfer_size;
    remaining -= transfer_size;
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

zx::status<> Gt6853Device::Write(const Register reg, const uint8_t* buffer, const size_t size) {
  uint16_t address = static_cast<uint16_t>(reg);
  size_t remaining = size;

  while (remaining > 0) {
    const size_t transfer_size = std::min(remaining, kI2cMaxTransferSize - sizeof(address));
    const uint16_t be_address = htobe16(address);

    uint8_t write_buffer[kI2cMaxTransferSize];
    memcpy(write_buffer, &be_address, sizeof(be_address));
    memcpy(write_buffer + sizeof(be_address), buffer, transfer_size);

    zx_status_t status = i2c_.WriteSync(write_buffer, sizeof(be_address) + transfer_size);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to write %zu bytes to 0x%02x: %d", transfer_size, address, status);
      return zx::error_status(status);
    }

    address += transfer_size;
    buffer += transfer_size;
    remaining -= transfer_size;
  }

  return zx::ok();
}

zx::status<> Gt6853Device::WriteAndCheck(Register reg, const uint8_t* buffer, size_t size) {
  zx::status<> write_status = Write(reg, buffer, size);
  if (write_status.is_error()) {
    return write_status;
  }

  uint16_t address = static_cast<uint16_t>(reg);
  size_t remaining = size;

  while (remaining > 0) {
    const size_t transfer_size = std::min(remaining, kI2cMaxTransferSize);
    const uint16_t be_address = htobe16(address);

    uint8_t read_buffer[kI2cMaxTransferSize];
    zx_status_t status = i2c_.WriteReadSync(reinterpret_cast<const uint8_t*>(&be_address),
                                            sizeof(be_address), read_buffer, transfer_size);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to read %zu bytes from 0x%02x: %d", transfer_size, address, status);
      return zx::error_status(status);
    }

    if (memcmp(read_buffer, buffer, transfer_size) != 0) {
      return zx::error_status(ZX_ERR_IO_DATA_INTEGRITY);
    }

    address += transfer_size;
    buffer += transfer_size;
    remaining -= transfer_size;
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

ZIRCON_DRIVER(Gt6853Device, touch::gt6853_driver_ops, "zircon", "0.1");
