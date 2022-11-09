// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/pinecrest.h"

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/stdcompat/span.h>

#include <algorithm>
#include <iterator>

#include <gpt/gpt.h>
#include <soc/aml-common/aml-guid.h>

#include "src/lib/uuid/uuid.h"
#include "src/storage/lib/paver/pave-logging.h"
#include "src/storage/lib/paver/pinecrest_abr_avbab_conversion.h"
#include "src/storage/lib/paver/utils.h"

namespace paver {
namespace {

using uuid::Uuid;

zx::result<AbrSlotIndex> QueryFirmwareSlot(fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root) {
  auto client_end = component::ConnectAt<fuchsia_boot::Arguments>(svc_root);
  if (!client_end.is_ok()) {
    ERROR("Failed to connect to boot argument service\n");
    return client_end.take_error();
  }
  fidl::WireSyncClient client{std::move(*client_end)};

  // Expect the bootloader to append a firmware slot item. Because CastOS only has A/B bootloader.
  // We can be A/B slot bootloader but booting a R kernel slot, in which case we can't tell from
  // libabr metadata.
  auto result = client->GetString(fidl::StringView{"zvb.firmware_slot"});
  if (!result.ok()) {
    ERROR("Failed to get firmware slot\n");
    return zx::error(result.status());
  }

  const auto response = result.Unwrap();
  if (response->value.get() == "_a") {
    return zx::ok(kAbrSlotIndexA);
  } else if (response->value.get() == "_b") {
    return zx::ok(kAbrSlotIndexB);
  }

  ERROR("Invalid firmware slot %s\n", response->value.data());
  return zx::error(ZX_ERR_INTERNAL);
}

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

zx::result<std::unique_ptr<PartitionClient>> PinecrestPartitioner::FindPartitionByGuid(
    const PartitionSpec& spec) const {
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

zx::result<std::unique_ptr<PartitionClient>> PinecrestPartitioner::FindPartitionByName(
    const PartitionSpec& spec) const {
  std::function<bool(const gpt_partition_t&)> filter;
  switch (spec.partition) {
    case Partition::kZirconA:
    case Partition::kZirconB:
    case Partition::kZirconR:
    case Partition::kVbMetaA:
    case Partition::kVbMetaB:
    case Partition::kVbMetaR:
    case Partition::kAbrMeta:
      filter = [&spec](const gpt_partition_t& part) {
        const auto partition_scheme = spec.partition == Partition::kAbrMeta
                                          ? PartitionScheme::kLegacy
                                          : PartitionScheme::kNew;
        const char* name = PartitionName(spec.partition, partition_scheme);
        return FilterByName(part, name);
      };
      break;
    case Partition::kFuchsiaVolumeManager:
      filter = IsFvmPartition;
      break;
    default:
      ERROR("Pinecrest partitioner cannot find unknown partition type\n");
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  auto status = gpt_->FindPartition(filter);
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok(std::move(status->partition));
}

zx::result<std::unique_ptr<PartitionClient>> PinecrestPartitioner::FindPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  auto partition = FindPartitionByGuid(spec);
  if (partition.is_ok()) {
    return partition;
  } else {
    return FindPartitionByName(spec);
  }
}

zx::result<> PinecrestPartitioner::WipeFvm() const { return gpt_->WipeFvm(); }

zx::result<> PinecrestPartitioner::InitPartitionTables() const {
  // GPT provisioning will be done by the bootloader.
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

zx::result<> PinecrestAbrClient::Read(const zx::vmo& vmo, size_t size) {
  if (size < sizeof(AbrData)) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  auto res = client_->Read(vmo, size);
  if (res.is_error()) {
    return res;
  }

  fzl::VmoMapper mapper;
  auto map_res = zx::make_result(mapper.Map(vmo));
  if (map_res.is_error()) {
    return map_res;
  }

  AbrData data;
  memcpy(&data, mapper.start(), sizeof(data));
  avbab_to_abr(&data);
  memcpy(mapper.start(), &data, sizeof(data));
  return zx::ok();
}

zx::result<> PinecrestAbrClient::Write(const zx::vmo& vmo, size_t vmo_size) {
  if (vmo_size < sizeof(AbrData)) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  fzl::VmoMapper mapper;
  auto map_res = zx::make_result(mapper.Map(vmo));
  if (map_res.is_error()) {
    return map_res;
  }

  AbrData data;
  memcpy(&data, mapper.start(), sizeof(data));
  if (!abr_to_avbab(&data, firmware_slot_)) {
    ERROR("Failed to convert libabr to avb ab\n");
    return zx::error(ZX_ERR_INTERNAL);
  }
  memcpy(mapper.start(), &data, sizeof(data));
  return client_->Write(vmo, vmo_size);
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

  // ABR metadata has no need of a content type since it's always local rather
  // than provided in an update package, so just use the default content type.
  auto partition = partitioner->FindPartition(paver::PartitionSpec(paver::Partition::kAbrMeta));
  if (partition.is_error()) {
    return partition.take_error();
  }

  auto firmware_slot = QueryFirmwareSlot(svc_root);
  if (firmware_slot.is_error()) {
    return firmware_slot.take_error();
  }

  return abr::AbrPartitionClient::Create(
      std::make_unique<PinecrestAbrClient>(std::move(partition.value()), *firmware_slot));
}

}  // namespace paver
