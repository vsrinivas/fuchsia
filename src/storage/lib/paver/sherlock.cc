// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/sherlock.h"

#include <fbl/span.h>
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

zx::status<std::unique_ptr<DevicePartitioner>> SherlockPartitioner::Initialize(
    fbl::unique_fd devfs_root, const zx::channel& svc_root, const fbl::unique_fd& block_device) {
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

  for (const auto& supported : supported_specs) {
    if (SpecMatches(spec, supported)) {
      return true;
    }
  }

  return false;
}

zx::status<std::unique_ptr<PartitionClient>> SherlockPartitioner::AddPartition(
    const PartitionSpec& spec) const {
  ERROR("Cannot add partitions to a sherlock device\n");
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<std::unique_ptr<PartitionClient>> SherlockPartitioner::FindPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  Uuid type;

  switch (spec.partition) {
    case Partition::kBootloaderA: {
      auto boot0_part = OpenBlockPartition(gpt_->devfs_root(), std::nullopt,
                                           Uuid(GUID_EMMC_BOOT1_VALUE), ZX_SEC(5));
      if (boot0_part.is_error()) {
        return boot0_part.take_error();
      }
      auto boot0 =
          std::make_unique<FixedOffsetBlockPartitionClient>(std::move(boot0_part.value()), 1);

      auto boot1_part = OpenBlockPartition(gpt_->devfs_root(), std::nullopt,
                                           Uuid(GUID_EMMC_BOOT2_VALUE), ZX_SEC(5));
      if (boot1_part.is_error()) {
        return boot1_part.take_error();
      }
      auto boot1 =
          std::make_unique<FixedOffsetBlockPartitionClient>(std::move(boot1_part.value()), 1);

      std::vector<std::unique_ptr<PartitionClient>> partitions;
      partitions.push_back(std::move(boot0));
      partitions.push_back(std::move(boot1));

      return zx::ok(std::make_unique<PartitionCopyClient>(std::move(partitions)));
    }
    case Partition::kZirconA:
      type = GUID_ZIRCON_A_VALUE;
      break;
    case Partition::kZirconB:
      type = GUID_ZIRCON_B_VALUE;
      break;
    case Partition::kZirconR:
      type = GUID_ZIRCON_R_VALUE;
      break;
    case Partition::kVbMetaA:
      type = GUID_VBMETA_A_VALUE;
      break;
    case Partition::kVbMetaB:
      type = GUID_VBMETA_B_VALUE;
      break;
    case Partition::kVbMetaR:
      type = GUID_VBMETA_R_VALUE;
      break;
    case Partition::kAbrMeta:
      type = GUID_ABR_META_VALUE;
      break;
    case Partition::kFuchsiaVolumeManager:
      type = GUID_FVM_VALUE;
      break;
    default:
      ERROR("Partition type is invalid\n");
      return zx::error(ZX_ERR_INVALID_ARGS);
  }

  const auto filter = [&type](const gpt_partition_t& part) { return type == Uuid(part.type); };
  auto status = gpt_->FindPartition(std::move(filter));
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok(std::move(status->partition));
}

zx::status<> SherlockPartitioner::WipeFvm() const { return gpt_->WipeFvm(); }

zx::status<> SherlockPartitioner::InitPartitionTables() const {
  struct Partition {
    const char* name;
    Uuid type;
    size_t min_size;
  };

  const auto add_partitions = [&](fbl::Span<const Partition> partitions) -> zx::status<> {
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

    for (const auto& partition_name : fbl::Span(partitions_to_wipe)) {
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

  if (auto status = add_partitions(fbl::Span<const Partition>(partitions_to_add));
      status.is_error()) {
    return status.take_error();
  }

  return zx::ok();
}

zx::status<> SherlockPartitioner::WipePartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> SherlockPartitioner::ValidatePayload(const PartitionSpec& spec,
                                                  fbl::Span<const uint8_t> data) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  return zx::ok();
}

zx::status<std::unique_ptr<DevicePartitioner>> SherlockPartitionerFactory::New(
    fbl::unique_fd devfs_root, const zx::channel& svc_root, Arch arch,
    std::shared_ptr<Context> context, const fbl::unique_fd& block_device) {
  return SherlockPartitioner::Initialize(std::move(devfs_root), svc_root, block_device);
}

zx::status<std::unique_ptr<abr::Client>> SherlockAbrClientFactory::New(
    fbl::unique_fd devfs_root, const zx::channel& svc_root,
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
