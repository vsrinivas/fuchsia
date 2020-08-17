// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device-partitioner.h"

#include <lib/fdio/fd.h>
#include <lib/zx/status.h>
#include <zircon/errors.h>

#include <memory>
#include <unordered_map>
#include <utility>

#include <fbl/span.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <gpt/gpt.h>

#include "src/lib/uuid/uuid.h"
#include "src/storage/lib/paver/pave-logging.h"
#include "src/storage/lib/paver/utils.h"

namespace paver {

using uuid::Uuid;

// Information for each Partition enum value.
struct PartitionInfo {
  // Default on-disk name and type GUID. Device aren't required to use these,
  // they may instead use the legacy values below or their own custom values.
  const char* name;
  Uuid type;

  // Legacy on-disk name and type GUID.
  const char* legacy_name;
  Uuid legacy_type;

  // Device-agnostic debug name. Something we can print to the logs that will
  // make sense for any device regardless of on-disk partition layout.
  const char* debug_name;
};

const PartitionInfo* GetPartitionInfo(Partition partition) {
  static std::unordered_map<Partition, PartitionInfo> map = {
      // TODO(fxbug.dev/52708): add support for bootloader A/B/R once we have devices
      // ready for it. For now just default to bootloader_a.
      {Partition::kBootloaderA, PartitionInfo{.name = GPT_BOOTLOADER_A_NAME,
                                              .type = GPT_BOOTLOADER_ABR_TYPE_GUID,
                                              .legacy_name = GUID_EFI_NAME,
                                              .legacy_type = GUID_BOOTLOADER_VALUE,
                                              .debug_name = "Bootloader A"}},
      {Partition::kBootloaderB, PartitionInfo{.name = GPT_BOOTLOADER_B_NAME,
                                              .type = GPT_BOOTLOADER_ABR_TYPE_GUID,
                                              .legacy_name = GUID_EFI_NAME,
                                              .legacy_type = GUID_BOOTLOADER_VALUE,
                                              .debug_name = "Bootloader B"}},
      {Partition::kBootloaderR, PartitionInfo{.name = GPT_BOOTLOADER_R_NAME,
                                              .type = GPT_BOOTLOADER_ABR_TYPE_GUID,
                                              .legacy_name = GUID_EFI_NAME,
                                              .legacy_type = GUID_BOOTLOADER_VALUE,
                                              .debug_name = "Bootloader R"}},

      {Partition::kZirconA, PartitionInfo{.name = GPT_ZIRCON_A_NAME,
                                          .type = GPT_ZIRCON_ABR_TYPE_GUID,
                                          .legacy_name = GUID_ZIRCON_A_NAME,
                                          .legacy_type = GUID_ZIRCON_A_VALUE,
                                          .debug_name = "Zircon A"}},
      {Partition::kZirconB, PartitionInfo{.name = GPT_ZIRCON_B_NAME,
                                          .type = GPT_ZIRCON_ABR_TYPE_GUID,
                                          .legacy_name = GUID_ZIRCON_B_NAME,
                                          .legacy_type = GUID_ZIRCON_B_VALUE,
                                          .debug_name = "Zircon B"}},
      {Partition::kZirconR, PartitionInfo{.name = GPT_ZIRCON_R_NAME,
                                          .type = GPT_ZIRCON_ABR_TYPE_GUID,
                                          .legacy_name = GUID_ZIRCON_R_NAME,
                                          .legacy_type = GUID_ZIRCON_R_VALUE,
                                          .debug_name = "Zircon R"}},

      {Partition::kVbMetaA, PartitionInfo{.name = GPT_VBMETA_A_NAME,
                                          .type = GPT_VBMETA_ABR_TYPE_GUID,
                                          .legacy_name = GUID_VBMETA_A_NAME,
                                          .legacy_type = GUID_VBMETA_A_VALUE,
                                          .debug_name = "VBMeta A"}},
      {Partition::kVbMetaB, PartitionInfo{.name = GPT_VBMETA_B_NAME,
                                          .type = GPT_VBMETA_ABR_TYPE_GUID,
                                          .legacy_name = GUID_VBMETA_B_NAME,
                                          .legacy_type = GUID_VBMETA_B_VALUE,
                                          .debug_name = "VBMeta B"}},
      {Partition::kVbMetaR, PartitionInfo{.name = GPT_VBMETA_R_NAME,
                                          .type = GPT_VBMETA_ABR_TYPE_GUID,
                                          .legacy_name = GUID_VBMETA_R_NAME,
                                          .legacy_type = GUID_VBMETA_R_VALUE,
                                          .debug_name = "VBMeta R"}},

      {Partition::kAbrMeta, PartitionInfo{.name = GPT_DURABLE_BOOT_NAME,
                                          .type = GPT_DURABLE_BOOT_TYPE_GUID,
                                          .legacy_name = GUID_ABR_META_NAME,
                                          .legacy_type = GUID_ABR_META_VALUE,
                                          .debug_name = "A/B/R Metadata"}},

      {Partition::kFuchsiaVolumeManager, PartitionInfo{.name = GPT_FVM_NAME,
                                                       .type = GPT_FVM_TYPE_GUID,
                                                       .legacy_name = GUID_FVM_NAME,
                                                       .legacy_type = GUID_FVM_VALUE,
                                                       .debug_name = "FVM"}},
  };

  auto iter = map.find(partition);
  if (iter == map.end()) {
    return nullptr;
  }
  return &iter->second;
}

const char* PartitionName(Partition partition, PartitionScheme scheme) {
  const PartitionInfo* info = GetPartitionInfo(partition);
  if (info) {
    return scheme == PartitionScheme::kNew ? info->name : info->legacy_name;
  }
  return "Unknown";
}

// Returns the given partition's type GUID.
zx::status<Uuid> PartitionTypeUuid(Partition partition, PartitionScheme scheme) {
  const PartitionInfo* info = GetPartitionInfo(partition);
  if (info) {
    return zx::ok(scheme == PartitionScheme::kNew ? info->type : info->legacy_type);
  }
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

fbl::String PartitionSpec::ToString() const {
  const char* debug_name = "<Unknown Partition>";
  const PartitionInfo* info = GetPartitionInfo(partition);
  if (info) {
    debug_name = info->debug_name;
  }

  if (content_type.empty()) {
    return debug_name;
  }
  return fbl::StringPrintf("%s (%.*s)", debug_name, static_cast<int>(content_type.size()),
                           content_type.data());
}

std::vector<std::unique_ptr<DevicePartitionerFactory>>*
DevicePartitionerFactory::registered_factory_list() {
  static std::vector<std::unique_ptr<DevicePartitionerFactory>>* registered_factory_list = nullptr;
  if (registered_factory_list == nullptr) {
    registered_factory_list = new std::vector<std::unique_ptr<DevicePartitionerFactory>>();
  }
  return registered_factory_list;
}

std::unique_ptr<DevicePartitioner> DevicePartitionerFactory::Create(
    fbl::unique_fd devfs_root, const zx::channel& svc_root, Arch arch,
    std::shared_ptr<Context> context, zx::channel block_device) {
  fbl::unique_fd block_dev;
  if (block_device) {
    int fd;
    zx_status_t status = fdio_fd_create(block_device.release(), &fd);
    if (status != ZX_OK) {
      ERROR(
          "Unable to create fd from block_device channel. Does it implement fuchsia.io.Node?: %s\n",
          zx_status_get_string(status));
      return nullptr;
    }
    block_dev.reset(fd);
  }

  for (auto& factory : *registered_factory_list()) {
    if (auto status = factory->New(devfs_root.duplicate(), svc_root, arch, context, block_dev);
        status.is_ok()) {
      return std::move(status.value());
    }
  }
  return nullptr;
}

void DevicePartitionerFactory::Register(std::unique_ptr<DevicePartitionerFactory> factory) {
  registered_factory_list()->push_back(std::move(factory));
}

/*====================================================*
 *               FIXED PARTITION MAP                  *
 *====================================================*/

constexpr PartitionScheme kFixedDevicePartitionScheme = PartitionScheme::kLegacy;

zx::status<std::unique_ptr<DevicePartitioner>> FixedDevicePartitioner::Initialize(
    fbl::unique_fd devfs_root) {
  if (HasSkipBlockDevice(devfs_root)) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  LOG("Successfully initialized FixedDevicePartitioner Device Partitioner\n");
  return zx::ok(new FixedDevicePartitioner(std::move(devfs_root)));
}

bool FixedDevicePartitioner::SupportsPartition(const PartitionSpec& spec) const {
  const PartitionSpec supported_specs[] = {PartitionSpec(paver::Partition::kBootloaderA),
                                           PartitionSpec(paver::Partition::kZirconA),
                                           PartitionSpec(paver::Partition::kZirconB),
                                           PartitionSpec(paver::Partition::kZirconR),
                                           PartitionSpec(paver::Partition::kVbMetaA),
                                           PartitionSpec(paver::Partition::kVbMetaB),
                                           PartitionSpec(paver::Partition::kVbMetaR),
                                           PartitionSpec(paver::Partition::kAbrMeta),
                                           PartitionSpec(paver::Partition::kFuchsiaVolumeManager)};

  for (const auto& supported : supported_specs) {
    if (SpecMatches(spec, supported)) {
      return true;
    }
  }

  return false;
}

zx::status<std::unique_ptr<PartitionClient>> FixedDevicePartitioner::AddPartition(
    const PartitionSpec& spec) const {
  ERROR("Cannot add partitions to a fixed-map partition device\n");
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<std::unique_ptr<PartitionClient>> FixedDevicePartitioner::FindPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  zx::status<Uuid> type_or = PartitionTypeUuid(spec.partition, kFixedDevicePartitionScheme);
  if (type_or.is_error()) {
    ERROR("partition_type is invalid!\n");
    return type_or.take_error();
  }
  Uuid type = type_or.value();

  zx::status<zx::channel> partition =
      OpenBlockPartition(devfs_root_, std::nullopt, type, ZX_SEC(5));
  if (partition.is_error()) {
    return partition.take_error();
  }

  return zx::ok(new BlockPartitionClient(std::move(partition.value())));
}

zx::status<> FixedDevicePartitioner::WipeFvm() const {
  if (auto status = WipeBlockPartition(devfs_root_, std::nullopt, Uuid(GUID_FVM_VALUE));
      status.is_error()) {
    ERROR("Failed to wipe FVM.\n");
  } else {
    LOG("Wiped FVM successfully.\n");
  }
  LOG("Immediate reboot strongly recommended\n");
  return zx::ok();
}

zx::status<> FixedDevicePartitioner::InitPartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> FixedDevicePartitioner::WipePartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> FixedDevicePartitioner::ValidatePayload(const PartitionSpec& spec,
                                                     fbl::Span<const uint8_t> data) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  return zx::ok();
}

zx::status<std::unique_ptr<DevicePartitioner>> DefaultPartitionerFactory::New(
    fbl::unique_fd devfs_root, const zx::channel& svc_root, Arch arch,
    std::shared_ptr<Context> context, const fbl::unique_fd& block_device) {
  return FixedDevicePartitioner::Initialize(std::move(devfs_root));
}

}  // namespace paver
