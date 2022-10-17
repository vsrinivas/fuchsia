// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/nelson.h"

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

zx::result<std::unique_ptr<DevicePartitioner>> NelsonPartitioner::Initialize(
    fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root,
    const fbl::unique_fd& block_device) {
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
  const PartitionSpec supported_specs[] = {
      PartitionSpec(paver::Partition::kBootloaderA, "bl2"),
      PartitionSpec(paver::Partition::kBootloaderA, "bootloader"),
      PartitionSpec(paver::Partition::kBootloaderB, "bootloader"),
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
  return std::any_of(std::cbegin(supported_specs), std::cend(supported_specs),
                     [&](const PartitionSpec& supported) { return SpecMatches(spec, supported); });
}

zx::result<std::unique_ptr<PartitionClient>> NelsonPartitioner::AddPartition(
    const PartitionSpec& spec) const {
  ERROR("Cannot add partitions to a nelson device\n");
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::result<std::unique_ptr<PartitionClient>> NelsonPartitioner::GetEmmcBootPartitionClient() const {
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

zx::result<std::unique_ptr<PartitionClient>> NelsonPartitioner::GetBootloaderPartitionClient(
    const PartitionSpec& spec) const {
  std::vector<std::unique_ptr<PartitionClient>> partitions;

  auto boot_status = GetEmmcBootPartitionClient();
  if (boot_status.is_error()) {
    ERROR("Failed to find emmc boot partition\n");
    return boot_status.take_error();
  }

  ZX_ASSERT(spec.partition == Partition::kBootloaderA || spec.partition == Partition::kBootloaderB);
  PartitionSpec tpl_partition_spec(spec.partition, "tpl");

  auto tpl_status = FindPartition(tpl_partition_spec);
  if (tpl_status.is_error()) {
    ERROR("Failed to find tpl partition\n");
    return tpl_status.take_error();
  }

  auto tpl_block_size_status = tpl_status.value()->GetBlockSize();
  if (tpl_block_size_status.is_error()) {
    ERROR("Failed to get block size for tpl\n");
    return tpl_block_size_status.take_error();
  }
  size_t block_size = tpl_block_size_status.value();
  // Casting to |BlockDevicePartitionClient| is safe because all branches
  // in |FindPartition| returns a block-device-based partition client.
  auto tpl = std::make_unique<FixedOffsetBlockPartitionClient>(
      static_cast<BlockDevicePartitionClient*>(tpl_status.value().get())->GetChannel(), 0,
      kNelsonBL2Size / block_size);

  return zx::ok(std::make_unique<NelsonBootloaderPartitionClient>(std::move(boot_status.value()),
                                                                  std::move(tpl)));
}

zx::result<std::unique_ptr<PartitionClient>> NelsonPartitioner::FindPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  if (spec.content_type == "bootloader") {
    return GetBootloaderPartitionClient(spec);
  }

  std::variant<std::string_view, Uuid> part_info;
  switch (spec.partition) {
    case Partition::kBootloaderA: {
      if (spec.content_type == "bl2") {
        return GetEmmcBootPartitionClient();
      } else if (spec.content_type == "tpl") {
        part_info = "tpl_a";
      } else {
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
      break;
    }
    case Partition::kBootloaderB: {
      if (spec.content_type == "tpl") {
        part_info = "tpl_b";
      } else {
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
      break;
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
    auto partition =
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

zx::result<> NelsonPartitioner::WipeFvm() const { return gpt_->WipeFvm(); }

zx::result<> NelsonPartitioner::InitPartitionTables() const {
  ERROR("Initializing gpt partitions from paver is not supported on nelson\n");
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::result<> NelsonPartitioner::WipePartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::result<> NelsonPartitioner::ValidatePayload(const PartitionSpec& spec,
                                                cpp20::span<const uint8_t> data) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  if (spec.content_type == "bootloader") {
    if (data.size() <= kNelsonBL2Size) {
      ERROR("Payload does not seem to contain tpl image\n");
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
  }

  return zx::ok();
}

zx::result<std::unique_ptr<DevicePartitioner>> NelsonPartitionerFactory::New(
    fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root, Arch arch,
    std::shared_ptr<Context> context, const fbl::unique_fd& block_device) {
  return NelsonPartitioner::Initialize(std::move(devfs_root), svc_root, block_device);
}

zx::result<std::unique_ptr<abr::Client>> NelsonAbrClientFactory::New(
    fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root,
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

zx::result<size_t> NelsonBootloaderPartitionClient::GetBlockSize() {
  return emmc_boot_client_->GetBlockSize();
}

zx::result<size_t> NelsonBootloaderPartitionClient::GetPartitionSize() {
  auto boot_partition_size_status = emmc_boot_client_->GetPartitionSize();
  if (boot_partition_size_status.is_error()) {
    return boot_partition_size_status.take_error();
  }

  auto tpl_user_partition_size_status = tpl_client_->GetPartitionSize();
  if (tpl_user_partition_size_status.is_error()) {
    return tpl_user_partition_size_status.take_error();
  }
  size_t partition_size = std::min(boot_partition_size_status.value(),
                                   tpl_user_partition_size_status.value() + paver::kNelsonBL2Size);
  return zx::ok(partition_size);
}

zx::result<> NelsonBootloaderPartitionClient::Trim() {
  if (auto status = emmc_boot_client_->Trim(); status.is_error()) {
    return status.take_error();
  }
  return tpl_client_->Trim();
}

zx::result<> NelsonBootloaderPartitionClient::Flush() {
  if (auto status = emmc_boot_client_->Flush(); status.is_error()) {
    return status.take_error();
  }
  return tpl_client_->Flush();
}

fidl::ClientEnd<fuchsia_hardware_block::Block> NelsonBootloaderPartitionClient::GetChannel() {
  ERROR("GetChannel() is not supported for NelsonBootloaderPartitionClient\n");
  ZX_ASSERT(false);
  return zx::channel();
}

fbl::unique_fd NelsonBootloaderPartitionClient::block_fd() {
  ERROR("block_fd() is not supported for NelsonBootloaderPartitionClient\n");
  ZX_ASSERT(false);
  return fbl::unique_fd();
}

zx::result<> NelsonBootloaderPartitionClient::Read(const zx::vmo& vmo, size_t size) {
  // Read from boot0/1 first
  if (auto status = emmc_boot_client_->Read(vmo, size); status.is_error()) {
    return status.take_error();
  }
  size_t tpl_read_size = size - std::min(kNelsonBL2Size, size);
  if (!CheckIfTplSame(vmo, tpl_read_size)) {
    LOG("User tpl differs from boot0/1 tpl. Conservatively refusing to read bootloader\n");
    return zx::error(ZX_ERR_BAD_STATE);
  }
  return zx::ok();
}

zx::result<> NelsonBootloaderPartitionClient::Write(const zx::vmo& vmo, size_t vmo_size) {
  // write to boot0/1 for the entire combined image.
  if (auto status = emmc_boot_client_->Write(vmo, vmo_size); status.is_error()) {
    return status.take_error();
  }
  // write to tpl for only the tpl part.
  // tpl_client adds an integral offset equal to the bl2 size when accessing vmo.
  // thus the size to write should be adjusted accordingly.
  auto buffer_offset_size_status = tpl_client_->GetBufferOffsetInBytes();
  if (buffer_offset_size_status.is_error()) {
    return buffer_offset_size_status.take_error();
  }
  size_t write_size = vmo_size - std::min(vmo_size, buffer_offset_size_status.value());
  return tpl_client_->Write(vmo, write_size);
}

bool NelsonBootloaderPartitionClient::CheckIfTplSame(const zx::vmo& vmo, size_t tpl_read_size) {
  if (!tpl_read_size) {
    return true;
  }

  // Use the size of |vmo| for creating read buffer because it has been adjusted to
  // consider block alignment.
  uint64_t vmo_size;
  if (auto status = vmo.get_size(&vmo_size); status != ZX_OK) {
    ERROR("Fail to get vmo_size for read buffer\n");
    return false;
  }

  fzl::OwnedVmoMapper read_tpl;
  if (auto status = read_tpl.CreateAndMap(vmo_size, "read-tpl"); status != ZX_OK) {
    ERROR("Fail to create vmo for tpl read\n");
    return false;
  }

  if (auto status = tpl_client_->Read(read_tpl.vmo(), tpl_read_size); status.is_error()) {
    ERROR("Fail to read tpl\n");
    return false;
  }

  // Compare the tpl part
  fzl::VmoMapper mapper;
  if (auto status = mapper.Map(vmo, kNelsonBL2Size, 0, ZX_VM_PERM_READ); status != ZX_OK) {
    ERROR("Fail to create vmo mapper\n")
    return false;
  }

  const uint8_t* tpl_start = static_cast<const uint8_t*>(read_tpl.start());
  return !memcmp(mapper.start(), tpl_start + kNelsonBL2Size, tpl_read_size);
}

}  // namespace paver
