// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/pinecrest.h"

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

zx::result<std::unique_ptr<DevicePartitioner>> PinecrestPartitioner::Initialize(
    fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root,
    const fbl::unique_fd& block_device) {
  auto status = IsBoard(devfs_root, "pinecrest");
  if (status.is_error()) {
    return status.take_error();
  }

  auto status_or_gpt =
      GptDevicePartitioner::InitializeGpt(std::move(devfs_root), svc_root, block_device);
  if (status_or_gpt.is_error()) {
    return status_or_gpt.take_error();
  }

  auto partitioner = WrapUnique(new PinecrestPartitioner(std::move(status_or_gpt->gpt)));

  LOG("Successfully initialized PinecrestPartitioner Device Partitioner\n");
  return zx::ok(std::move(partitioner));
}

bool PinecrestPartitioner::SupportsPartition(const PartitionSpec& spec) const {
  const PartitionSpec supported_specs[] = {
      PartitionSpec(paver::Partition::kZirconA),
      PartitionSpec(paver::Partition::kZirconB),
      PartitionSpec(paver::Partition::kZirconR),
      PartitionSpec(paver::Partition::kVbMetaA),
      PartitionSpec(paver::Partition::kVbMetaB),
      PartitionSpec(paver::Partition::kVbMetaR),
      PartitionSpec(paver::Partition::kAbrMeta),
      PartitionSpec(paver::Partition::kFuchsiaVolumeManager),
      PartitionSpec(paver::Partition::kFuchsiaVolumeManager, kOpaqueVolumeContentType),
  };
  return std::any_of(std::cbegin(supported_specs), std::cend(supported_specs),
                     [&](const PartitionSpec& supported) { return SpecMatches(spec, supported); });
}

zx::result<std::unique_ptr<PartitionClient>> PinecrestPartitioner::AddPartition(
    const PartitionSpec& spec) const {
  ERROR("Cannot add partitions to a pinecrest device\n");
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::result<std::unique_ptr<PartitionClient>> PinecrestPartitioner::FindPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  Uuid part_info;
  switch (spec.partition) {
    // TODO(fxbug.dev/111512): Also support bootloader partitions.
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

  auto partition = OpenBlockPartition(gpt_->devfs_root(), std::nullopt, part_info, ZX_SEC(5));
  if (partition.is_error()) {
    return partition.take_error();
  }
  return zx::ok(new BlockPartitionClient(std::move(partition.value())));
}

zx::result<> PinecrestPartitioner::WipeFvm() const { return gpt_->WipeFvm(); }

zx::result<> PinecrestPartitioner::InitPartitionTables() const {
  // TODO(fxbug.dev/111512): Implement this to initialize partition tables, since we don't have
  // bootloader fastboot.
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::result<> PinecrestPartitioner::WipePartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::result<> PinecrestPartitioner::ValidatePayload(const PartitionSpec& spec,
                                                   cpp20::span<const uint8_t> data) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  return zx::ok();
}

zx::result<std::unique_ptr<DevicePartitioner>> PinecrestPartitionerFactory::New(
    fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root, Arch arch,
    std::shared_ptr<Context> context, const fbl::unique_fd& block_device) {
  return PinecrestPartitioner::Initialize(std::move(devfs_root), svc_root, block_device);
}

zx::result<std::unique_ptr<abr::Client>> PinecrestAbrClientFactory::New(
    fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root,
    std::shared_ptr<paver::Context> context) {
  fbl::unique_fd none;
  auto partitioner =
      PinecrestPartitioner::Initialize(std::move(devfs_root), std::move(svc_root), none);

  if (partitioner.is_error()) {
    return partitioner.take_error();
  }

  // TODO(fxbug.dev/111512): Provide some translation to make sure that our libabr scheme is
  // backward compatible. Instead of libabr, CastOS's earlier stage bootloader uses avb ab, which
  // has the same meta data structure, but (1) uses little endian for crc, and (2) doesn't have the
  // concept of a recovery slot (it will brick if both slots are marked unbootable).
  // ABR metadata has no need of a content type since it's always local rather
  // than provided in an update package, so just use the default content type.
  auto partition = partitioner->FindPartition(paver::PartitionSpec(paver::Partition::kAbrMeta));
  if (partition.is_error()) {
    return partition.take_error();
  }

  return abr::AbrPartitionClient::Create(std::move(partition.value()));
}

}  // namespace paver
