// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/chromebook-x64.h"

#include <zircon/hw/gpt.h>

#include <algorithm>
#include <iterator>
#include <set>

#include <gpt/cros.h>

#include "fidl/fuchsia.vboot/cpp/wire.h"
#include "src/lib/uuid/uuid.h"
#include "src/storage/lib/paver/abr-client-vboot.h"
#include "src/storage/lib/paver/abr-client.h"
#include "src/storage/lib/paver/flashmap-client.h"
#include "src/storage/lib/paver/pave-logging.h"
#include "src/storage/lib/paver/utils.h"
#include "src/storage/lib/paver/validation.h"

namespace paver {

namespace {

using uuid::Uuid;

namespace block = fuchsia_hardware_block;

constexpr size_t kKibibyte = 1024;
constexpr size_t kMebibyte = kKibibyte * 1024;
constexpr size_t kGibibyte = kMebibyte * 1024;

// Minimum size for the ChromeOS state partition.
constexpr size_t kMinStateSize = 5 * kGibibyte;

// Chromebook uses the new partition scheme.
constexpr PartitionScheme kPartitionScheme = PartitionScheme::kNew;

zx::result<Uuid> CrosPartitionType(Partition type) {
  switch (type) {
    case Partition::kZirconA:
    case Partition::kZirconB:
    case Partition::kZirconR:
      return zx::ok(Uuid(GUID_CROS_KERNEL_VALUE));
    default:
      return GptPartitionType(type, kPartitionScheme);
  }
}

}  // namespace

zx::result<std::unique_ptr<DevicePartitioner>> CrosDevicePartitioner::Initialize(
    fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root, Arch arch,
    const fbl::unique_fd& block_device) {
  if (arch != Arch::kX64) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  zx::result<> status = IsBootloader(devfs_root, "coreboot");
  if (status.is_error()) {
    return status.take_error();
  }

  auto status_or_gpt =
      GptDevicePartitioner::InitializeGpt(std::move(devfs_root), svc_root, block_device);
  if (status_or_gpt.is_error()) {
    return status_or_gpt.take_error();
  }
  std::unique_ptr<GptDevicePartitioner>& gpt_partitioner = status_or_gpt->gpt;

  // Determine if the firmware supports A/B in Zircon.
  auto active_slot = abr::QueryBootConfig(gpt_partitioner->devfs_root(), svc_root);
  bool supports_abr = active_slot.is_ok();
  auto partitioner =
      WrapUnique(new CrosDevicePartitioner(std::move(gpt_partitioner), supports_abr));

  // If the GPT is a new GPT, initialize the device's partition tables.
  if (status_or_gpt->initialize_partition_tables) {
    if (auto status = partitioner->InitPartitionTables(); status.is_error()) {
      return status.take_error();
    }
  }

  LOG("Successfully initialized CrOS Device Partitioner\n");
  return zx::ok(std::move(partitioner));
}

bool CrosDevicePartitioner::SupportsPartition(const PartitionSpec& spec) const {
  const PartitionSpec supported_specs[] = {
      PartitionSpec(paver::Partition::kBootloaderA, "ap"),
      PartitionSpec(paver::Partition::kZirconA),
      PartitionSpec(paver::Partition::kZirconB),
      PartitionSpec(paver::Partition::kZirconR),
      PartitionSpec(paver::Partition::kVbMetaA),
      PartitionSpec(paver::Partition::kVbMetaB),
      PartitionSpec(paver::Partition::kVbMetaR),
      PartitionSpec(paver::Partition::kFuchsiaVolumeManager),
      PartitionSpec(paver::Partition::kFuchsiaVolumeManager, kOpaqueVolumeContentType),
  };
  return std::any_of(std::cbegin(supported_specs), std::cend(supported_specs),
                     [&](const PartitionSpec& supported) { return SpecMatches(spec, supported); });
}  // namespace paver

zx::result<std::unique_ptr<PartitionClient>> CrosDevicePartitioner::AddPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // NOTE: If you update the minimum sizes of partitions, please update the
  // CrosDevicePartitionerTests.InitPartitionTables test.
  size_t minimum_size_bytes = 0;
  switch (spec.partition) {
    case Partition::kZirconA:
    case Partition::kZirconB:
    case Partition::kZirconR:
      minimum_size_bytes = 64 * kMebibyte;
      break;
    case Partition::kFuchsiaVolumeManager:
      minimum_size_bytes = 56 * kGibibyte;
      break;
    case Partition::kVbMetaA:
    case Partition::kVbMetaB:
    case Partition::kVbMetaR:
      minimum_size_bytes = 64 * kKibibyte;
      break;
    default:
      ERROR("Cros partitioner cannot add unknown partition type\n");
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  const char* name = PartitionName(spec.partition, kPartitionScheme);
  auto type = CrosPartitionType(spec.partition);
  if (type.is_error()) {
    return type.take_error();
  }

  zx_status_t status = ZX_OK;
  zx::result<std::unique_ptr<PartitionClient>> result;
  do {
    if (status != ZX_OK) {
      // If AddPartition fails because we're out of space (ZX_ERR_NO_RESOURCES), we try to shrink
      // the ChromeOS STATE partition in order to make room.
      auto shrink_result = ShrinkCrosState();
      if (shrink_result.is_error()) {
        // Shrink failed for some reason - bail out.
        ERROR("Failed to shrink CrOS state: %s", shrink_result.status_string());
        return shrink_result.take_error();
      }

      bool did_shrink = *shrink_result;
      if (!did_shrink) {
        // The STATE partition is as small as we can make it, so bail out.
        ERROR("Refusing to shrink CrOS state partition below its minimum size.");
        return zx::error(ZX_ERR_NO_RESOURCES);
      }
    }

    result =
        gpt_->AddPartition(name, type.value(), minimum_size_bytes, /*optional_reserve_bytes*/ 0);
    status = result.is_ok() ? ZX_OK : result.error_value();
  } while (status == ZX_ERR_NO_RESOURCES);
  return result;
}

zx::result<std::unique_ptr<PartitionClient>> CrosDevicePartitioner::FindPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  switch (spec.partition) {
    case Partition::kZirconA:
    case Partition::kZirconB:
    case Partition::kZirconR: {
      const auto filter = [&spec](const gpt_partition_t& part) {
        const char* name = PartitionName(spec.partition, kPartitionScheme);
        auto partition_type = CrosPartitionType(spec.partition);
        return partition_type.is_ok() && FilterByTypeAndName(part, partition_type.value(), name);
      };
      auto status = gpt_->FindPartition(filter);
      if (status.is_error()) {
        return status.take_error();
      }
      return zx::ok(std::move(status->partition));
    }
    case Partition::kFuchsiaVolumeManager: {
      auto status = gpt_->FindPartition(IsFvmPartition);
      if (status.is_error()) {
        return status.take_error();
      }
      return zx::ok(std::move(status->partition));
    }
    case Partition::kVbMetaA:
    case Partition::kVbMetaB:
    case Partition::kVbMetaR: {
      const auto filter = [&spec](const gpt_partition_t& part) {
        const char* name = PartitionName(spec.partition, kPartitionScheme);
        auto status = CrosPartitionType(spec.partition);
        return status.is_ok() && FilterByTypeAndName(part, status.value(), name);
      };
      auto status = gpt_->FindPartition(filter);
      if (status.is_error()) {
        return status.take_error();
      }
      return zx::ok(std::move(status->partition));
    }
    case Partition::kBootloaderA: {
      if (spec.content_type == "ap") {
        return FlashmapPartitionClient::Create(gpt_->devfs_root(), gpt_->svc_root(), zx::sec(15));
      }
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }
    default:
      ERROR("Cros partitioner cannot find unknown partition type\n");
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
}

zx::result<> CrosDevicePartitioner::FinalizePartition(const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // Special partition finalization is only necessary for Zircon partitions, and only when we
  // don't support A/B.
  if (spec.partition != Partition::kZirconA || supports_abr_) {
    return zx::ok();
  }

  // Find the Zircon A kernel partition.
  const char* name = PartitionName(Partition::kZirconA, kPartitionScheme);
  const auto filter = [name](const gpt_partition_t& part) {
    return FilterByTypeAndName(part, GUID_CROS_KERNEL_VALUE, name);
  };
  auto status = gpt_->FindPartition(filter);
  if (status.is_error()) {
    ERROR("Cannot find %s partition\n", name);
    return status.take_error();
  }
  gpt_partition_t* zircon_a_partition = status->gpt_partition;

  // Get kernel partition priorities.
  gpt_partition_t* priority_to_partition[16] = {nullptr};

  for (uint32_t i = 0; i < gpt::kPartitionCount; ++i) {
    zx::result<gpt_partition_t*> partition_or = gpt_->GetGpt()->GetPartition(i);
    if (partition_or.is_error()) {
      continue;
    }
    gpt_partition_t* partition = partition_or.value();
    const uint8_t priority = gpt_cros_attr_get_priority(partition->flags);

    // Priority 0 means not bootable.
    if (priority == 0) {
      continue;
    }

    // Ignore anything not of type CROS KERNEL.
    if (Uuid(partition->type) != Uuid(GUID_CROS_KERNEL_VALUE)) {
      continue;
    }

    // We'll later set the priority of Zircon A to be higher than any other partition.
    if (partition == zircon_a_partition) {
      continue;
    }

    priority_to_partition[priority] = partition;
  }

  // Compact kernel partition priorities.
  uint8_t priority = 1;
  for (gpt_partition_t* partition : priority_to_partition) {
    if (partition) {
      int ret = gpt_cros_attr_set_priority(&partition->flags, priority);
      if (ret != 0) {
        ERROR("Cannot set CrOS partition priority\n");
        return zx::error(ZX_ERR_OUT_OF_RANGE);
      }
      ++priority;
    }
  }

  // Priority for Zircon A set to higher priority than all other kernels.
  int ret = gpt_cros_attr_set_priority(&zircon_a_partition->flags, priority);
  if (ret != 0) {
    ERROR("Cannot set CrOS partition priority for ZIRCON-A\n");
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  // Successful set to 'true' to encourage the bootloader to
  // use this partition.
  gpt_cros_attr_set_successful(&zircon_a_partition->flags, true);
  // Maximize the number of attempts to boot this partition before
  // we fall back to a different kernel.
  ret = gpt_cros_attr_set_tries(&zircon_a_partition->flags, 15);
  if (ret != 0) {
    ERROR("Cannot set CrOS partition 'tries' for ZIRCON-A\n");
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  if (auto status = zx::make_result(gpt_->GetGpt()->Sync()); status.is_error()) {
    ERROR("Failed to sync CrOS partition 'tries' for ZIRCON-A\n");
    return status.take_error();
  }

  return zx::ok();
}

zx::result<> CrosDevicePartitioner::WipeFvm() const { return gpt_->WipeFvm(); }

zx::result<> CrosDevicePartitioner::InitPartitionTables() const {
  // Wipe partitions.
  // CrosDevicePartitioner operates on partition names.
  const std::set<std::string_view> partitions_to_wipe{
      GPT_VBMETA_A_NAME,
      GPT_VBMETA_B_NAME,
      GPT_VBMETA_R_NAME,
      GPT_ZIRCON_A_NAME,
      GPT_ZIRCON_B_NAME,
      GPT_ZIRCON_R_NAME,
      GPT_FVM_NAME,
      // Partition names from the legacy scheme.
      GUID_ZIRCON_A_NAME,
      GUID_ZIRCON_B_NAME,
      GUID_ZIRCON_R_NAME,
      GUID_FVM_NAME,
      // These additional partition names are based on the legacy, legacy naming scheme.
      "ZIRCON-A",
      "ZIRCON-B",
      "ZIRCON-R",
      "fvm",
      "SYSCFG",
  };
  auto status = gpt_->WipePartitions([&partitions_to_wipe](const gpt_partition_t& part) {
    char cstring_name[GPT_NAME_LEN] = {};
    utf16_to_cstring(cstring_name, part.name, GPT_NAME_LEN);
    bool result = partitions_to_wipe.find(cstring_name) != partitions_to_wipe.end();
    return result;
  });
  if (status.is_error()) {
    ERROR("Failed to wipe partitions: %s\n", status.status_string());
    return status.take_error();
  }

  // Add partitions with default content type.
  const std::array<PartitionSpec, 7> partitions_to_add = {
      PartitionSpec(Partition::kZirconA),
      PartitionSpec(Partition::kZirconB),
      PartitionSpec(Partition::kZirconR),
      PartitionSpec(Partition::kVbMetaA),
      PartitionSpec(Partition::kVbMetaB),
      PartitionSpec(Partition::kVbMetaR),
      PartitionSpec(Partition::kFuchsiaVolumeManager),
  };
  for (auto spec : partitions_to_add) {
    auto status = AddPartition(spec);
    if (status.is_error()) {
      ERROR("Failed to create partition \"%s\": %s\n", spec.ToString().c_str(),
            status.status_string());
      return status.take_error();
    }
  }

  LOG("Successfully initialized GPT\n");
  return zx::ok();
}

zx::result<> CrosDevicePartitioner::WipePartitionTables() const {
  return gpt_->WipePartitionTables();
}

zx::result<> CrosDevicePartitioner::ValidatePayload(const PartitionSpec& spec,
                                                    cpp20::span<const uint8_t> data) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  if (IsZirconPartitionSpec(spec)) {
    if (!IsValidChromeOSKernel(data)) {
      return zx::error(ZX_ERR_BAD_STATE);
    }
  }

  return zx::ok();
}

zx::result<bool> CrosDevicePartitioner::ShrinkCrosState() const {
  constexpr const char* name = "STATE";
  const auto filter = [](const gpt_partition_t& part) {
    constexpr uuid::Uuid kCrosStateGuid(GUID_CROS_STATE_VALUE);
    constexpr uuid::Uuid kLinuxStateGuid(GUID_LINUX_FILESYSTEM_DATA_VALUE);
    return FilterByTypeAndName(part, kCrosStateGuid, name) ||
           FilterByTypeAndName(part, kLinuxStateGuid, name);
  };

  auto status = gpt_->FindPartition(filter);
  if (status.is_error()) {
    return status.take_error();
  }

  gpt_partition_t* part = status->gpt_partition;
  size_t minimum_size_blocks = kMinStateSize / gpt_->GetBlockInfo().block_size;
  size_t cur_size_blocks = (part->last - part->first) + 1;

  // Halve the partition's size. STATE should always be at the end of the disk, so cut the first
  // half out.
  size_t new_size_blocks = std::max(cur_size_blocks / 2, minimum_size_blocks);
  part->first += new_size_blocks;

  // Bail out if the partition is already as small as it can be.
  if (new_size_blocks == cur_size_blocks) {
    return zx::ok(false);
  }

  // Pause the block watcher before touching the GPT.
  auto pauser = BlockWatcherPauser::Create(gpt_->svc_root());
  if (pauser.is_error()) {
    ERROR("Failed to pause the block watcher");
    return pauser.take_error();
  }
  zx_status_t result = gpt_->GetGpt()->Sync();
  if (result != ZX_OK) {
    return zx::error(result);
  }

  return zx::ok(true);
}

zx::result<std::unique_ptr<DevicePartitioner>> ChromebookX64PartitionerFactory::New(
    fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root, Arch arch,
    std::shared_ptr<Context> context, const fbl::unique_fd& block_device) {
  return CrosDevicePartitioner::Initialize(std::move(devfs_root), svc_root, arch, block_device);
}

zx::result<std::unique_ptr<abr::Client>> ChromebookX64AbrClientFactory::New(
    fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root,
    std::shared_ptr<paver::Context> context) {
  fbl::unique_fd none;
  auto partitioner = CrosDevicePartitioner::Initialize(std::move(devfs_root), std::move(svc_root),
                                                       paver::Arch::kX64, none);

  if (partitioner.is_error()) {
    return partitioner.take_error();
  }

  return abr::VbootClient::Create(std::unique_ptr<CrosDevicePartitioner>(
      static_cast<CrosDevicePartitioner*>(partitioner.value().release())));
}

}  // namespace paver
