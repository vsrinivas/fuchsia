// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/vim3.h"

#include <lib/fzl/owned-vmo-mapper.h>
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

}  // namespace

zx::result<std::unique_ptr<DevicePartitioner>> Vim3Partitioner::Initialize(
    fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root,
    const fbl::unique_fd& block_device) {
  auto status = IsBoard(devfs_root, "vim3");
  if (status.is_error()) {
    return status.take_error();
  }

  auto status_or_gpt =
      GptDevicePartitioner::InitializeGpt(std::move(devfs_root), svc_root, block_device);
  if (status_or_gpt.is_error()) {
    return status_or_gpt.take_error();
  }

  auto partitioner = WrapUnique(new Vim3Partitioner(std::move(status_or_gpt->gpt)));

  LOG("Successfully initialized Vim3Partitioner Device Partitioner\n");
  return zx::ok(std::move(partitioner));
}

bool Vim3Partitioner::SupportsPartition(const PartitionSpec& spec) const {
  const PartitionSpec supported_specs[] = {PartitionSpec(paver::Partition::kBootloaderA),
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

zx::result<std::unique_ptr<PartitionClient>> Vim3Partitioner::AddPartition(
    const PartitionSpec& spec) const {
  ERROR("Cannot add partitions to a vim3 device\n");
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::result<std::unique_ptr<PartitionClient>> Vim3Partitioner::GetEmmcBootPartitionClient() const {
  auto boot0_part =
      OpenBlockPartition(gpt_->devfs_root(), std::nullopt, Uuid(GUID_EMMC_BOOT1_VALUE), ZX_SEC(5));
  if (boot0_part.is_error()) {
    return boot0_part.take_error();
  }
  auto boot0 =
      std::make_unique<FixedOffsetBlockPartitionClient>(std::move(boot0_part.value()), 1, 0);

  auto boot1_part =
      OpenBlockPartition(gpt_->devfs_root(), std::nullopt, Uuid(GUID_EMMC_BOOT2_VALUE), ZX_SEC(5));
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

zx::result<std::unique_ptr<PartitionClient>> Vim3Partitioner::FindPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  std::string_view part_name;

  switch (spec.partition) {
    case Partition::kBootloaderA:
      return GetEmmcBootPartitionClient();
    case Partition::kZirconA:
      part_name = GPT_ZIRCON_A_NAME;
      break;
    case Partition::kZirconB:
      part_name = GPT_ZIRCON_B_NAME;
      break;
    case Partition::kZirconR:
      part_name = GPT_ZIRCON_R_NAME;
      break;
    case Partition::kVbMetaA:
      part_name = GPT_VBMETA_A_NAME;
      break;
    case Partition::kVbMetaB:
      part_name = GPT_VBMETA_B_NAME;
      break;
    case Partition::kVbMetaR:
      part_name = GPT_VBMETA_R_NAME;
      break;
    case Partition::kAbrMeta:
      part_name = GPT_DURABLE_BOOT_NAME;
      break;
    case Partition::kFuchsiaVolumeManager:
      part_name = GPT_FVM_NAME;
      break;
    default:
      ERROR("Partition type is invalid\n");
      return zx::error(ZX_ERR_INVALID_ARGS);
  }

  const auto filter_by_name = [part_name](const gpt_partition_t& part) {
    char cstring_name[GPT_NAME_LEN] = {};
    utf16_to_cstring(cstring_name, part.name, GPT_NAME_LEN);
    return part_name == std::string_view(cstring_name);
  };

  auto status = gpt_->FindPartition(std::move(filter_by_name));
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok(std::move(status->partition));
}

zx::result<> Vim3Partitioner::WipeFvm() const { return gpt_->WipeFvm(); }

zx::result<> Vim3Partitioner::InitPartitionTables() const {
  ERROR("Initializing gpt partitions from paver is not supported on vim3\n");
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::result<> Vim3Partitioner::WipePartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::result<> Vim3Partitioner::ValidatePayload(const PartitionSpec& spec,
                                              cpp20::span<const uint8_t> data) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  return zx::ok();
}

zx::result<std::unique_ptr<DevicePartitioner>> Vim3PartitionerFactory::New(
    fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root, Arch arch,
    std::shared_ptr<Context> context, const fbl::unique_fd& block_device) {
  return Vim3Partitioner::Initialize(std::move(devfs_root), svc_root, block_device);
}

zx::result<std::unique_ptr<abr::Client>> Vim3AbrClientFactory::New(
    fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root,
    std::shared_ptr<paver::Context> context) {
  fbl::unique_fd none;
  auto partitioner = Vim3Partitioner::Initialize(std::move(devfs_root), std::move(svc_root), none);

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
