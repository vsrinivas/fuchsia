// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/nelson.h"

#include <fbl/span.h>
#include <gpt/gpt.h>
#include <soc/aml-common/aml-guid.h>

#include "src/lib/uuid/uuid.h"
#include "src/storage/lib/paver/pave-logging.h"
#include "src/storage/lib/paver/utils.h"

namespace paver {
namespace {

using uuid::Uuid;

}  // namespace

zx::status<std::unique_ptr<DevicePartitioner>> NelsonPartitioner::Initialize(
    fbl::unique_fd devfs_root, const zx::channel& svc_root, const fbl::unique_fd& block_device) {
  auto status = IsBoard(devfs_root, "nelson");
  if (status.is_error()) {
    return status.take_error();
  }

  auto status_or_gpt =
      GptDevicePartitioner::InitializeGpt(std::move(devfs_root), svc_root, block_device);
  if (status_or_gpt.is_error()) {
    return status_or_gpt.take_error();
  }

  auto partitioner = WrapUnique(new NelsonPartitioner(std::move(status_or_gpt->gpt)));

  LOG("Successfully initialized NelsonPartitioner Device Partitioner\n");
  return zx::ok(std::move(partitioner));
}

bool NelsonPartitioner::SupportsPartition(const PartitionSpec& spec) const {
  const PartitionSpec supported_specs[] = {PartitionSpec(paver::Partition::kBootloaderA, "bl2"),
                                           PartitionSpec(paver::Partition::kBootloaderA, "tpl"),
                                           PartitionSpec(paver::Partition::kBootloaderB, "tpl"),
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

zx::status<std::unique_ptr<PartitionClient>> NelsonPartitioner::AddPartition(
    const PartitionSpec& spec) const {
  ERROR("Cannot add partitions to a nelson device\n");
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<std::unique_ptr<PartitionClient>> NelsonPartitioner::GetBL2PartitionClient() const {
  auto boot0_part =
      OpenBlockPartition(gpt_->devfs_root(), std::nullopt, Uuid(GUID_EMMC_BOOT1_VALUE), ZX_SEC(5));
  if (boot0_part.is_error()) {
    return boot0_part.take_error();
  }
  auto boot0 = std::make_unique<FixedOffsetBlockPartitionClient>(std::move(boot0_part.value()), 1);

  auto boot1_part =
      OpenBlockPartition(gpt_->devfs_root(), std::nullopt, Uuid(GUID_EMMC_BOOT2_VALUE), ZX_SEC(5));
  if (boot1_part.is_error()) {
    return boot1_part.take_error();
  }
  auto boot1 = std::make_unique<FixedOffsetBlockPartitionClient>(std::move(boot1_part.value()), 1);

  std::vector<std::unique_ptr<PartitionClient>> partitions;
  partitions.push_back(std::move(boot0));
  partitions.push_back(std::move(boot1));

  return zx::ok(std::make_unique<PartitionCopyClient>(std::move(partitions)));
}

zx::status<std::unique_ptr<PartitionClient>> NelsonPartitioner::FindPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  std::variant<std::string_view, Uuid> part_info;

  switch (spec.partition) {
    case Partition::kBootloaderA: {
      if (spec.content_type == "bl2") {
        return GetBL2PartitionClient();
      } else if (spec.content_type == "tpl") {
        part_info = "tpl_a";
      } else {
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
    }
    case Partition::kBootloaderB: {
      if (spec.content_type == "tpl") {
        part_info = "tpl_b";
      } else {
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
    }
    case Partition::kZirconA:
      part_info = Uuid(GUID_ZIRCON_A_VALUE);
      break;
    case Partition::kZirconB:
      part_info = Uuid(GUID_ZIRCON_B_VALUE);
      break;
    case Partition::kZirconR:
      part_info = Uuid(GUID_ZIRCON_R_VALUE);
      break;
    case Partition::kVbMetaA:
      part_info = Uuid(GUID_VBMETA_A_VALUE);
      break;
    case Partition::kVbMetaB:
      part_info = Uuid(GUID_VBMETA_B_VALUE);
      break;
    case Partition::kVbMetaR:
      part_info = Uuid(GUID_VBMETA_R_VALUE);
      break;
    case Partition::kAbrMeta:
      part_info = Uuid(GUID_ABR_META_VALUE);
      break;
    case Partition::kFuchsiaVolumeManager:
      part_info = Uuid(GUID_FVM_VALUE);
      break;
    default:
      ERROR("Partition type is invalid\n");
      return zx::error(ZX_ERR_INVALID_ARGS);
  }

  const auto filter_by_name = [&part_info](const gpt_partition_t& part) {
    char cstring_name[GPT_NAME_LEN] = {};
    utf16_to_cstring(cstring_name, part.name, GPT_NAME_LEN);
    return std::get<std::string_view>(part_info) == std::string_view(cstring_name);
  };

  if (std::holds_alternative<Uuid>(part_info)) {
    zx::status<zx::channel> partition =
        OpenBlockPartition(gpt_->devfs_root(), std::nullopt, std::get<Uuid>(part_info), ZX_SEC(5));
    if (partition.is_error()) {
      return partition.take_error();
    }
    return zx::ok(new BlockPartitionClient(std::move(partition.value())));
  } else {
    auto status = gpt_->FindPartition(std::move(filter_by_name));
    if (status.is_error()) {
      return status.take_error();
    }
    return zx::ok(std::move(status->partition));
  }
}

zx::status<> NelsonPartitioner::WipeFvm() const { return gpt_->WipeFvm(); }

zx::status<> NelsonPartitioner::InitPartitionTables() const {
  ERROR("Initializing gpt partitions from paver is not supported on nelson\n");
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> NelsonPartitioner::WipePartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> NelsonPartitioner::ValidatePayload(const PartitionSpec& spec,
                                                fbl::Span<const uint8_t> data) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  return zx::ok();
}

zx::status<std::unique_ptr<DevicePartitioner>> NelsonPartitionerFactory::New(
    fbl::unique_fd devfs_root, const zx::channel& svc_root, Arch arch,
    std::shared_ptr<Context> context, const fbl::unique_fd& block_device) {
  return NelsonPartitioner::Initialize(std::move(devfs_root), svc_root, block_device);
}

zx::status<std::unique_ptr<abr::Client>> NelsonAbrClientFactory::New(
    fbl::unique_fd devfs_root, const zx::channel& svc_root,
    std::shared_ptr<paver::Context> context) {
  fbl::unique_fd none;
  auto partitioner =
      NelsonPartitioner::Initialize(std::move(devfs_root), std::move(svc_root), none);

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
