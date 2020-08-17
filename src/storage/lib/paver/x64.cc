// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/x64.h"

#include "src/lib/uuid/uuid.h"
#include "src/storage/lib/paver/pave-logging.h"
#include "src/storage/lib/paver/utils.h"
#include "src/storage/lib/paver/validation.h"

namespace paver {

namespace {

using uuid::Uuid;

constexpr size_t kKibibyte = 1024;
constexpr size_t kMebibyte = kKibibyte * 1024;
constexpr size_t kGibibyte = kMebibyte * 1024;

// All X64 boards currently use the legacy partition scheme.
constexpr PartitionScheme kPartitionScheme = PartitionScheme::kLegacy;

// TODO: Remove support after July 9th 2021.
constexpr char kOldEfiName[] = "efi-system";

}  // namespace

zx::status<std::unique_ptr<DevicePartitioner>> EfiDevicePartitioner::Initialize(
    fbl::unique_fd devfs_root, const zx::channel& svc_root, Arch arch,
    const fbl::unique_fd& block_device) {
  if (arch != Arch::kX64) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  auto status = GptDevicePartitioner::InitializeGpt(std::move(devfs_root), svc_root, block_device);
  if (status.is_error()) {
    return status.take_error();
  }

  auto partitioner = WrapUnique(new EfiDevicePartitioner(arch, std::move(status->gpt)));
  if (status->initialize_partition_tables) {
    if (auto status = partitioner->InitPartitionTables(); status.is_error()) {
      return status.take_error();
    }
  }

  LOG("Successfully initialized EFI Device Partitioner\n");
  return zx::ok(std::move(partitioner));
}

bool EfiDevicePartitioner::SupportsPartition(const PartitionSpec& spec) const {
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

zx::status<std::unique_ptr<PartitionClient>> EfiDevicePartitioner::AddPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // NOTE: If you update the minimum sizes of partitions, please update the
  // EfiDevicePartitionerTests.InitPartitionTables test.
  size_t minimum_size_bytes = 0;
  switch (spec.partition) {
    case Partition::kBootloaderA:
      minimum_size_bytes = 16 * kMebibyte;
      break;
    case Partition::kZirconA:
      minimum_size_bytes = 128 * kMebibyte;
      break;
    case Partition::kZirconB:
      minimum_size_bytes = 128 * kMebibyte;
      break;
    case Partition::kZirconR:
      minimum_size_bytes = 192 * kMebibyte;
      break;
    case Partition::kVbMetaA:
      minimum_size_bytes = 64 * kKibibyte;
      break;
    case Partition::kVbMetaB:
      minimum_size_bytes = 64 * kKibibyte;
      break;
    case Partition::kVbMetaR:
      minimum_size_bytes = 64 * kKibibyte;
      break;
    case Partition::kAbrMeta:
      minimum_size_bytes = 4 * kKibibyte;
      break;
    case Partition::kFuchsiaVolumeManager:
      minimum_size_bytes = 16 * kGibibyte;
      break;
    default:
      ERROR("EFI partitioner cannot add unknown partition type\n");
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  const char* name = PartitionName(spec.partition, kPartitionScheme);
  auto type = GptPartitionType(spec.partition);
  if (type.is_error()) {
    return type.take_error();
  }
  return gpt_->AddPartition(name, type.value(), minimum_size_bytes, /*optional_reserve_bytes*/ 0);
}

zx::status<std::unique_ptr<PartitionClient>> EfiDevicePartitioner::FindPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  switch (spec.partition) {
    case Partition::kBootloaderA: {
      const auto filter = [](const gpt_partition_t& part) {
        return FilterByTypeAndName(part, GUID_EFI_VALUE, GUID_EFI_NAME) ||
               // TODO: Remove support after July 9th 2021.
               FilterByTypeAndName(part, GUID_EFI_VALUE, kOldEfiName);
      };
      auto status = gpt_->FindPartition(filter);
      if (status.is_error()) {
        return status.take_error();
      }
      return zx::ok(std::move(status->partition));
    }
    case Partition::kZirconA:
    case Partition::kZirconB:
    case Partition::kZirconR:
    case Partition::kVbMetaA:
    case Partition::kVbMetaB:
    case Partition::kVbMetaR:
    case Partition::kAbrMeta: {
      const auto filter = [&spec](const gpt_partition_t& part) {
        auto status = GptPartitionType(spec.partition);
        return status.is_ok() && FilterByType(part, status.value());
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
    default:
      ERROR("EFI partitioner cannot find unknown partition type\n");
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
}

zx::status<> EfiDevicePartitioner::FinalizePartition(const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  return zx::make_status(gpt_->GetGpt()->Sync());
}

zx::status<> EfiDevicePartitioner::WipeFvm() const { return gpt_->WipeFvm(); }

zx::status<> EfiDevicePartitioner::InitPartitionTables() const {
  const std::array<Partition, 9> partitions_to_add{
      Partition::kBootloaderA, Partition::kZirconA, Partition::kZirconB,
      Partition::kZirconR,     Partition::kVbMetaA, Partition::kVbMetaB,
      Partition::kVbMetaR,     Partition::kAbrMeta, Partition::kFuchsiaVolumeManager,
  };

  // Wipe partitions.
  // EfiDevicePartitioner operates on partition types.
  auto status = gpt_->WipePartitions([&partitions_to_add](const gpt_partition_t& part) {
    for (auto& partition : partitions_to_add) {
      // Get the partition type GUID, and compare it.
      auto status = GptPartitionType(partition);
      if (status.is_error() || status.value() != Uuid(part.type)) {
        continue;
      }
      // If we are wiping any non-bootloader partition, we are done.
      if (partition != Partition::kBootloaderA) {
        return true;
      }
      // If we are wiping the bootloader partition, only do so if it is the
      // Fuchsia-installed bootloader partition. This is to allow dual-booting.
      char cstring_name[GPT_NAME_LEN] = {};
      utf16_to_cstring(cstring_name, part.name, GPT_NAME_LEN);
      if (strncasecmp(cstring_name, GUID_EFI_NAME, GPT_NAME_LEN) == 0) {
        return true;
      }
      // Support the old name.
      // TODO: Remove support after July 9th 2021.
      if (strncasecmp(cstring_name, kOldEfiName, GPT_NAME_LEN) == 0) {
        return true;
      }
    }
    return false;
  });
  if (status.is_error()) {
    ERROR("Failed to wipe partitions: %s\n", status.status_string());
    return status.take_error();
  }

  // Add partitions with default content_type.
  for (auto type : partitions_to_add) {
    auto status = AddPartition(PartitionSpec(type));
    if (status.status_value() == ZX_ERR_ALREADY_BOUND) {
      ERROR("Warning: Skipping existing partition \"%s\"\n", PartitionName(type, kPartitionScheme));
    } else if (status.is_error()) {
      ERROR("Failed to create partition \"%s\": %s\n", PartitionName(type, kPartitionScheme),
            status.status_string());
      return status.take_error();
    }
  }

  LOG("Successfully initialized GPT\n");
  return zx::ok();
}  // namespace paver

zx::status<> EfiDevicePartitioner::WipePartitionTables() const {
  return gpt_->WipePartitionTables();
}

zx::status<> EfiDevicePartitioner::ValidatePayload(const PartitionSpec& spec,
                                                   fbl::Span<const uint8_t> data) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  if (IsZirconPartitionSpec(spec)) {
    if (!IsValidKernelZbi(arch_, data)) {
      return zx::error(ZX_ERR_BAD_STATE);
    }
  }

  return zx::ok();
}

zx::status<std::unique_ptr<DevicePartitioner>> X64PartitionerFactory::New(
    fbl::unique_fd devfs_root, const zx::channel& svc_root, Arch arch,
    std::shared_ptr<Context> context, const fbl::unique_fd& block_device) {
  return EfiDevicePartitioner::Initialize(std::move(devfs_root), svc_root, arch, block_device);
}

zx::status<std::unique_ptr<abr::Client>> X64AbrClientFactory::New(
    fbl::unique_fd devfs_root, const zx::channel& svc_root,
    std::shared_ptr<paver::Context> context) {
  fbl::unique_fd none;
  auto partitioner = EfiDevicePartitioner::Initialize(std::move(devfs_root), std::move(svc_root),
                                                      paver::Arch::kX64, none);

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
