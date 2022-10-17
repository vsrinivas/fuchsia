// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/sherlock.h"

#include <lib/stdcompat/span.h>

#include <algorithm>
#include <iterator>

#include <gpt/gpt.h>
#include <soc/aml-common/aml-guid.h>

#include "src/lib/uuid/uuid.h"
#include "src/storage/lib/paver/pave-logging.h"
#include "src/storage/lib/paver/utils.h"

namespace paver {
namespace {

using uuid::Uuid;

constexpr size_t kKibibyte = 1024;
constexpr size_t kMebibyte = kKibibyte * 1024;

}  // namespace

zx::result<std::unique_ptr<DevicePartitioner>> SherlockPartitioner::Initialize(
    fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root,
    const fbl::unique_fd& block_device) {
  auto status = IsBoard(devfs_root, "sherlock");
  if (status.is_error()) {
    return status.take_error();
  }

  auto status_or_gpt =
      GptDevicePartitioner::InitializeGpt(std::move(devfs_root), svc_root, block_device);
  if (status_or_gpt.is_error()) {
    return status_or_gpt.take_error();
  }

  auto partitioner = WrapUnique(new SherlockPartitioner(std::move(status_or_gpt->gpt)));
  if (status_or_gpt->initialize_partition_tables) {
    if (auto status = partitioner->InitPartitionTables(); status.is_error()) {
      return status.take_error();
    }
  }

  LOG("Successfully initialized SherlockPartitioner Device Partitioner\n");
  return zx::ok(std::move(partitioner));
}

// Sherlock bootloader types:
//
// -- default [deprecated] --
// The combined BL2 + TPL image.
//
// This was never actually added to any update packages, because older
// SherlockBootloaderPartitionClient implementations had a bug where they would
// write this image to the wrong place in flash which would overwrite critical
// metadata and brick the device on reboot.
//
// In order to prevent this from happening when updating older devices, never
// use this bootloader type on Sherlock.
//
// -- "skip_metadata" --
// The combined BL2 + TPL image.
//
// The image itself is identical to the default, but adding the "skip_metadata"
// type ensures that older pavers will ignore this image, and only newer
// implementations which properly skip the metadata section will write it.
bool SherlockPartitioner::SupportsPartition(const PartitionSpec& spec) const {
  const PartitionSpec supported_specs[] = {
      PartitionSpec(paver::Partition::kBootloaderA, "skip_metadata"),
      PartitionSpec(paver::Partition::kZirconA),
      PartitionSpec(paver::Partition::kZirconB),
      PartitionSpec(paver::Partition::kZirconR),
      PartitionSpec(paver::Partition::kVbMetaA),
      PartitionSpec(paver::Partition::kVbMetaB),
      PartitionSpec(paver::Partition::kVbMetaR),
      PartitionSpec(paver::Partition::kAbrMeta),
      PartitionSpec(paver::Partition::kFuchsiaVolumeManager)};
  return std::any_of(std::cbegin(supported_specs), std::cend(supported_specs),
                     [&](const PartitionSpec& supported) { return SpecMatches(spec, supported); });
}

zx::result<std::unique_ptr<PartitionClient>> SherlockPartitioner::AddPartition(
    const PartitionSpec& spec) const {
  ERROR("Cannot add partitions to a sherlock device\n");
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::result<std::unique_ptr<PartitionClient>> SherlockPartitioner::FindPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // TODO(b/173125535): Remove legacy GPT support
  Uuid legacy_type;
  std::string_view part_name;
  std::string_view secondary_part_name;

  switch (spec.partition) {
    case Partition::kBootloaderA: {
      auto boot0_part = OpenBlockPartition(gpt_->devfs_root(), std::nullopt,
                                           Uuid(GUID_EMMC_BOOT1_VALUE), ZX_SEC(5));
      if (boot0_part.is_error()) {
        return boot0_part.take_error();
      }
      auto boot0 =
          std::make_unique<FixedOffsetBlockPartitionClient>(std::move(boot0_part.value()), 1, 0);

      auto boot1_part = OpenBlockPartition(gpt_->devfs_root(), std::nullopt,
                                           Uuid(GUID_EMMC_BOOT2_VALUE), ZX_SEC(5));
      if (boot1_part.is_error()) {
        return boot1_part.take_error();
      }
      auto boot1 =
          std::make_unique<FixedOffsetBlockPartitionClient>(std::move(boot1_part.value()), 1, 0);

      std::vector<std::unique_ptr<PartitionClient>> partitions;
      partitions.push_back(std::move(boot0));
      partitions.push_back(std::move(boot1));

      return zx::ok(std::make_unique<PartitionCopyClient>(std::move(partitions)));
    }
    case Partition::kZirconA:
      legacy_type = GUID_ZIRCON_A_VALUE;
      part_name = GPT_ZIRCON_A_NAME;
      secondary_part_name = "boot";
      break;
    case Partition::kZirconB:
      legacy_type = GUID_ZIRCON_B_VALUE;
      part_name = GPT_ZIRCON_B_NAME;
      secondary_part_name = "system";
      break;
    case Partition::kZirconR:
      legacy_type = GUID_ZIRCON_R_VALUE;
      part_name = GPT_ZIRCON_R_NAME;
      secondary_part_name = "recovery";
      break;
    case Partition::kVbMetaA:
      legacy_type = GUID_VBMETA_A_VALUE;
      part_name = GPT_VBMETA_A_NAME;
      break;
    case Partition::kVbMetaB:
      legacy_type = GUID_VBMETA_B_VALUE;
      part_name = GPT_VBMETA_B_NAME;
      break;
    case Partition::kVbMetaR:
      legacy_type = GUID_VBMETA_R_VALUE;
      part_name = GPT_VBMETA_R_NAME;
      break;
    case Partition::kAbrMeta:
      legacy_type = GUID_ABR_META_VALUE;
      part_name = GPT_DURABLE_BOOT_NAME;
      break;
    case Partition::kFuchsiaVolumeManager:
      legacy_type = GUID_FVM_VALUE;
      part_name = GPT_FVM_NAME;
      break;
    default:
      ERROR("Partition type is invalid\n");
      return zx::error(ZX_ERR_INVALID_ARGS);
  }

  const auto filter = [&legacy_type, &part_name,
                       &secondary_part_name](const gpt_partition_t& part) {
    // Only filter by partition name instead of name + type due to bootloader bug (b/173801312)
    return FilterByType(part, legacy_type) || FilterByName(part, part_name) ||
           FilterByName(part, secondary_part_name);
  };
  auto status = gpt_->FindPartition(std::move(filter));
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok(std::move(status->partition));
}

zx::result<> SherlockPartitioner::WipeFvm() const { return gpt_->WipeFvm(); }

zx::result<> SherlockPartitioner::InitPartitionTables() const {
  struct Partition {
    const char* name;
    Uuid type;
    size_t min_size;
  };

  const auto add_partitions = [&](cpp20::span<const Partition> partitions) -> zx::result<> {
    for (const auto& part : partitions) {
      if (auto status = gpt_->AddPartition(part.name, part.type, part.min_size, 0);
          status.is_error()) {
        return status.take_error();
      }
    }
    return zx::ok();
  };

  const char* partitions_to_wipe[] = {
      "recovery",
      "boot",
      "system",
      "fvm",
      GUID_FVM_NAME,
      "cache",
      "fct",
      GUID_SYS_CONFIG_NAME,
      GUID_ABR_META_NAME,
      GUID_VBMETA_A_NAME,
      GUID_VBMETA_B_NAME,
      GUID_VBMETA_R_NAME,
      "migration",
      "buf",
      "buffer",
  };
  const auto wipe = [&partitions_to_wipe](const gpt_partition_t& part) {
    char cstring_name[GPT_NAME_LEN] = {};
    utf16_to_cstring(cstring_name, part.name, GPT_NAME_LEN);

    for (const auto& partition_name : cpp20::span(partitions_to_wipe)) {
      if (strncmp(cstring_name, partition_name, GPT_NAME_LEN) == 0) {
        return true;
      }
    }
    return false;
  };

  if (auto status = gpt_->WipePartitions(wipe); status.is_error()) {
    return status.take_error();
  }

  const Partition partitions_to_add[] = {
      {
          "recovery",
          GUID_ZIRCON_R_VALUE,
          32 * kMebibyte,
      },
      {
          "boot",
          GUID_ZIRCON_A_VALUE,
          32 * kMebibyte,
      },
      {
          "system",
          GUID_ZIRCON_B_VALUE,
          32 * kMebibyte,
      },
      {
          GUID_FVM_NAME,
          GUID_FVM_VALUE,
          3280 * kMebibyte,
      },
      {
          "fct",
          GUID_AMLOGIC_VALUE,
          64 * kMebibyte,
      },
      {
          GUID_SYS_CONFIG_NAME,
          GUID_SYS_CONFIG_VALUE,
          828 * kKibibyte,
      },
      {
          GUID_ABR_META_NAME,
          GUID_ABR_META_VALUE,
          4 * kKibibyte,
      },
      {
          GUID_VBMETA_A_NAME,
          GUID_VBMETA_A_VALUE,
          64 * kKibibyte,
      },
      {
          GUID_VBMETA_B_NAME,
          GUID_VBMETA_B_VALUE,
          64 * kKibibyte,
      },
      {
          GUID_VBMETA_R_NAME,
          GUID_VBMETA_R_VALUE,
          64 * kKibibyte,
      },
      {
          "migration",
          GUID_AMLOGIC_VALUE,
          7 * kMebibyte,
      },
      {
          "buffer",
          GUID_AMLOGIC_VALUE,
          48 * kMebibyte,
      },
  };

  if (auto status = add_partitions(cpp20::span<const Partition>(partitions_to_add));
      status.is_error()) {
    return status.take_error();
  }

  return zx::ok();
}

zx::result<> SherlockPartitioner::WipePartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::result<> SherlockPartitioner::ValidatePayload(const PartitionSpec& spec,
                                                  cpp20::span<const uint8_t> data) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  return zx::ok();
}

zx::result<std::unique_ptr<DevicePartitioner>> SherlockPartitionerFactory::New(
    fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root, Arch arch,
    std::shared_ptr<Context> context, const fbl::unique_fd& block_device) {
  return SherlockPartitioner::Initialize(std::move(devfs_root), svc_root, block_device);
}

zx::result<std::unique_ptr<abr::Client>> SherlockAbrClientFactory::New(
    fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root,
    std::shared_ptr<paver::Context> context) {
  fbl::unique_fd none;
  auto partitioner =
      SherlockPartitioner::Initialize(std::move(devfs_root), std::move(svc_root), none);

  if (partitioner.is_error()) {
    return partitioner.take_error();
  }

  // ABR metadata has no need of a content type since it's always local rather
  // than provided in an update package, so just use the default content type.
  auto partition = partitioner->FindPartition(paver::PartitionSpec(paver::Partition::kAbrMeta));
  if (partition.is_error()) {
    return partition.take_error();
  }

  return abr::AbrPartitionClient::Create(std::move(partition.value()));
}

}  // namespace paver
