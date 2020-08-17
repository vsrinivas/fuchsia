// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/gpt.h"

#include <dirent.h>
#include <fuchsia/device/llcpp/fidl.h>

#include <fbl/auto_call.h>
#include <gpt/c/gpt.h>

#include "src/storage/lib/paver/pave-logging.h"
#include "src/storage/lib/paver/utils.h"

namespace paver {

namespace {

using uuid::Uuid;

namespace block = ::llcpp::fuchsia::hardware::block;
namespace device = ::llcpp::fuchsia::device;

constexpr size_t ReservedHeaderBlocks(size_t blk_size) {
  constexpr size_t kReservedEntryBlocks = (16 * 1024);
  return (kReservedEntryBlocks + 2 * blk_size) / blk_size;
}

}  // namespace

zx::status<Uuid> GptPartitionType(Partition type) {
  switch (type) {
    case Partition::kBootloaderA:
      return zx::ok(Uuid(GUID_EFI_VALUE));
    case Partition::kZirconA:
      return zx::ok(Uuid(GUID_ZIRCON_A_VALUE));
    case Partition::kZirconB:
      return zx::ok(Uuid(GUID_ZIRCON_B_VALUE));
    case Partition::kZirconR:
      return zx::ok(Uuid(GUID_ZIRCON_R_VALUE));
    case Partition::kVbMetaA:
      return zx::ok(Uuid(GUID_VBMETA_A_VALUE));
    case Partition::kVbMetaB:
      return zx::ok(Uuid(GUID_VBMETA_B_VALUE));
    case Partition::kVbMetaR:
      return zx::ok(Uuid(GUID_VBMETA_R_VALUE));
    case Partition::kAbrMeta:
      return zx::ok(Uuid(GUID_ABR_META_VALUE));
    case Partition::kFuchsiaVolumeManager:
      return zx::ok(Uuid(GUID_FVM_VALUE));
    default:
      ERROR("Partition type is invalid\n");
      return zx::error(ZX_ERR_INVALID_ARGS);
  }
}

bool FilterByTypeAndName(const gpt_partition_t& part, const Uuid& type, fbl::StringPiece name) {
  char cstring_name[GPT_NAME_LEN];
  ::utf16_to_cstring(cstring_name, reinterpret_cast<const uint16_t*>(part.name), GPT_NAME_LEN);
  return type == Uuid(part.type) &&
         // We use a case-insenstive comparison to be compatible with the previous naming scheme.
         // On a ChromeOS device, all of the kernel partitions share a common GUID type, so we
         // distinguish Zircon kernel partitions based on name.
         strncasecmp(cstring_name, name.data(), name.length()) == 0;
}

zx::status<> RebindGptDriver(const zx::channel& svc_root, zx::unowned_channel chan) {
  auto pauser = BlockWatcherPauser::Create(svc_root);
  if (pauser.is_error()) {
    return pauser.take_error();
  }
  auto result = ::llcpp::fuchsia::device::Controller::Call::Rebind(
      std::move(chan), fidl::StringView("/boot/driver/gpt.so"));
  return zx::make_status(result.ok() ? (result->result.is_err() ? result->result.err() : ZX_OK)
                                     : result.status());
}

bool GptDevicePartitioner::FindGptDevices(const fbl::unique_fd& devfs_root, GptDevices* out) {
  constexpr char kBlockDevPath[] = "class/block/";
  fbl::unique_fd d_fd(openat(devfs_root.get(), kBlockDevPath, O_RDONLY));
  if (!d_fd) {
    ERROR("Cannot inspect block devices\n");
    return false;
  }
  DIR* d = fdopendir(d_fd.release());
  if (d == nullptr) {
    ERROR("Cannot inspect block devices\n");
    return false;
  }
  const auto closer = fbl::MakeAutoCall([&]() { closedir(d); });

  struct dirent* de;
  GptDevices found_devices;
  while ((de = readdir(d)) != nullptr) {
    fbl::unique_fd fd(openat(dirfd(d), de->d_name, O_RDWR));
    if (!fd) {
      continue;
    }
    fdio_cpp::FdioCaller caller(std::move(fd));

    auto result = block::Block::Call::GetInfo(caller.channel());
    if (!result.ok()) {
      continue;
    }
    const auto& response = result.value();
    if (response.status != ZX_OK) {
      continue;
    }
    if (response.info->flags & BLOCK_FLAG_REMOVABLE) {
      continue;
    }
    auto result2 = device::Controller::Call::GetTopologicalPath(caller.channel());
    if (result2.status() != ZX_OK) {
      continue;
    }
    const auto& response2 = result2.value();
    if (response2.result.is_err()) {
      continue;
    }

    std::string path_str(response2.result.response().path.data(),
                         static_cast<size_t>(response2.result.response().path.size()));

    // The GPT which will be a non-removable block device that isn't a partition itself.
    if (path_str.find("part-") == std::string::npos) {
      found_devices.push_back(std::make_pair(path_str, caller.release()));
    }
  }

  if (found_devices.empty()) {
    ERROR("No candidate GPT found\n");
    return false;
  }

  *out = std::move(found_devices);
  return true;
}

zx::status<std::unique_ptr<GptDevicePartitioner>> GptDevicePartitioner::InitializeProvidedGptDevice(
    fbl::unique_fd devfs_root, const zx::channel& svc_root, fbl::unique_fd gpt_device) {
  auto pauser = BlockWatcherPauser::Create(svc_root);
  if (pauser.is_error()) {
    ERROR("Failed to pause the block watcher\n");
    return pauser.take_error();
  }
  fdio_cpp::UnownedFdioCaller caller(gpt_device.get());
  auto result = block::Block::Call::GetInfo(caller.channel());
  if (!result.ok()) {
    ERROR("Warning: Could not acquire GPT block info: %s\n", zx_status_get_string(result.status()));
    return zx::error(result.status());
  }
  const auto& response = result.value();
  if (response.status != ZX_OK) {
    ERROR("Warning: Could not acquire GPT block info: %s\n", zx_status_get_string(response.status));
    return zx::error(response.status);
  }

  std::unique_ptr<GptDevice> gpt;
  if (GptDevice::Create(gpt_device.get(), response.info->block_size, response.info->block_count,
                        &gpt) != ZX_OK) {
    ERROR("Failed to get GPT info\n");
    return zx::error(ZX_ERR_BAD_STATE);
  }

  if (!gpt->Valid()) {
    ERROR("Located GPT is invalid; Attempting to initialize\n");
    if (gpt->RemoveAllPartitions() != ZX_OK) {
      ERROR("Failed to create empty GPT\n");
      return zx::error(ZX_ERR_BAD_STATE);
    }
    if (gpt->Sync() != ZX_OK) {
      ERROR("Failed to sync empty GPT\n");
      return zx::error(ZX_ERR_BAD_STATE);
    }
    if (auto status = RebindGptDriver(svc_root, caller.channel()); status.is_error()) {
      ERROR("Failed to re-read GPT\n");
      return status.take_error();
    }
    printf("Rebound GPT driver succesfully\n");
  }

  return zx::ok(new GptDevicePartitioner(devfs_root.duplicate(), svc_root, std::move(gpt_device),
                                         std::move(gpt), *(response.info)));
}

zx::status<GptDevicePartitioner::InitializeGptResult> GptDevicePartitioner::InitializeGpt(
    fbl::unique_fd devfs_root, const zx::channel& svc_root, const fbl::unique_fd& block_device) {
  if (block_device) {
    auto status =
        InitializeProvidedGptDevice(std::move(devfs_root), svc_root, block_device.duplicate());
    if (status.is_error()) {
      return status.take_error();
    }
    return zx::ok(InitializeGptResult{std::move(status.value()), false});
  }

  GptDevices gpt_devices;
  if (!FindGptDevices(devfs_root, &gpt_devices)) {
    ERROR("Failed to find GPT\n");
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  std::vector<fbl::unique_fd> non_removable_gpt_devices;

  std::unique_ptr<GptDevicePartitioner> gpt_partitioner;
  for (auto& [_, gpt_device] : gpt_devices) {
    fdio_cpp::UnownedFdioCaller caller(gpt_device.get());
    auto result = block::Block::Call::GetInfo(caller.channel());
    if (!result.ok()) {
      ERROR("Warning: Could not acquire GPT block info: %s\n",
            zx_status_get_string(result.status()));
      return zx::error(result.status());
    }
    const auto& response = result.value();
    if (response.status != ZX_OK) {
      ERROR("Warning: Could not acquire GPT block info: %s\n",
            zx_status_get_string(response.status));
      return zx::error(response.status);
    }

    if ((response.info->flags & block::FLAG_REMOVABLE) != 0) {
      continue;
    }

    std::unique_ptr<GptDevice> gpt;
    if (GptDevice::Create(gpt_device.get(), response.info->block_size, response.info->block_count,
                          &gpt) != ZX_OK) {
      ERROR("Failed to get GPT info\n");
      return zx::error(ZX_ERR_BAD_STATE);
    }

    if (!gpt->Valid()) {
      continue;
    }

    non_removable_gpt_devices.emplace_back(gpt_device.duplicate());

    auto partitioner = WrapUnique(new GptDevicePartitioner(
        devfs_root.duplicate(), svc_root, std::move(gpt_device), std::move(gpt), *(response.info)));

    if (partitioner->FindPartition(IsFvmPartition).is_error()) {
      continue;
    }

    if (gpt_partitioner) {
      ERROR("Found multiple block devices with valid GPTs. Unsuppported.\n");
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }
    gpt_partitioner = std::move(partitioner);
  }

  if (gpt_partitioner) {
    return zx::ok(InitializeGptResult{std::move(gpt_partitioner), false});
  }

  if (non_removable_gpt_devices.size() == 1) {
    // If we only find a single non-removable gpt device, we initialize it's partition table.
    auto status = InitializeProvidedGptDevice(std::move(devfs_root), svc_root,
                                              std::move(non_removable_gpt_devices[0]));
    if (status.is_error()) {
      return status.take_error();
    }
    return zx::ok(InitializeGptResult{std::move(status.value()), true});
  }

  ERROR(
      "Unable to find a valid GPT on this device with the expected partitions. "
      "Please run *one* of the following command(s):\n");

  for (const auto& [gpt_path, _] : gpt_devices) {
    ERROR("install-disk-image init-partition-tables --block-device %s\n", gpt_path.c_str());
  }

  return zx::error(ZX_ERR_NOT_FOUND);
}

struct PartitionPosition {
  size_t start;   // Block, inclusive
  size_t length;  // In Blocks
};

zx::status<GptDevicePartitioner::FindFirstFitResult> GptDevicePartitioner::FindFirstFit(
    size_t bytes_requested) const {
  LOG("Looking for space\n");
  // Gather GPT-related information.
  size_t blocks_requested = (bytes_requested + block_info_.block_size - 1) / block_info_.block_size;

  // Sort all partitions by starting block.
  // For simplicity, include the 'start' and 'end' reserved spots as
  // partitions.
  size_t partition_count = 0;
  PartitionPosition partitions[gpt::kPartitionCount + 2];
  const size_t reserved_blocks = ReservedHeaderBlocks(block_info_.block_size);
  partitions[partition_count].start = 0;
  partitions[partition_count++].length = reserved_blocks;
  partitions[partition_count].start = block_info_.block_count - reserved_blocks;
  partitions[partition_count++].length = reserved_blocks;

  for (uint32_t i = 0; i < gpt::kPartitionCount; i++) {
    const gpt_partition_t* p = gpt_->GetPartition(i);
    if (!p) {
      continue;
    }
    partitions[partition_count].start = p->first;
    partitions[partition_count].length = p->last - p->first + 1;
    LOG("Partition seen with start %zu, end %zu (length %zu)\n", p->first, p->last,
        partitions[partition_count].length);
    partition_count++;
  }
  LOG("Sorting\n");
  qsort(partitions, partition_count, sizeof(PartitionPosition), [](const void* p1, const void* p2) {
    ssize_t s1 = static_cast<ssize_t>(static_cast<const PartitionPosition*>(p1)->start);
    ssize_t s2 = static_cast<ssize_t>(static_cast<const PartitionPosition*>(p2)->start);
    return s1 > s2 ? +1 : -1;
  });

  // Look for space between the partitions. Since the reserved spots of the
  // GPT were included in |partitions|, all available space will be located
  // "between" partitions.
  for (size_t i = 0; i < partition_count - 1; i++) {
    const size_t next = partitions[i].start + partitions[i].length;
    LOG("Partition[%zu] From Block [%zu, %zu) ... (next partition starts at block %zu)\n", i,
        partitions[i].start, next, partitions[i + 1].start);

    if (next > partitions[i + 1].start) {
      ERROR("Corrupted GPT\n");
      return zx::error(ZX_ERR_IO);
    }
    const size_t free_blocks = partitions[i + 1].start - next;
    LOG("    There are %zu free blocks (%zu requested)\n", free_blocks, blocks_requested);
    if (free_blocks >= blocks_requested) {
      return zx::ok(FindFirstFitResult{next, free_blocks});
    }
  }
  ERROR("No GPT space found\n");
  return zx::error(ZX_ERR_NO_RESOURCES);
}

zx::status<Uuid> GptDevicePartitioner::CreateGptPartition(const char* name, const Uuid& type,
                                                          uint64_t offset, uint64_t blocks) const {
  Uuid guid = Uuid::Generate();

  zx_status_t status;
  if ((status = gpt_->AddPartition(name, type.bytes(), guid.bytes(), offset, blocks, 0)) != ZX_OK) {
    ERROR("Failed to add partition\n");
    return zx::error(ZX_ERR_IO);
  }
  if ((status = gpt_->Sync()) != ZX_OK) {
    ERROR("Failed to sync GPT\n");
    return zx::error(ZX_ERR_IO);
  }
  if (auto status = zx::make_status(gpt_->ClearPartition(offset, 1)); status.is_error()) {
    ERROR("Failed to clear first block of new partition\n");
    return status.take_error();
  }
  if (auto status = RebindGptDriver(svc_root_, Channel()); status.is_error()) {
    ERROR("Failed to rebind GPT\n");
    return status.take_error();
  }

  return zx::ok(guid);
}

zx::status<std::unique_ptr<PartitionClient>> GptDevicePartitioner::AddPartition(
    const char* name, const Uuid& type, size_t minimum_size_bytes,
    size_t optional_reserve_bytes) const {
  auto status = FindFirstFit(minimum_size_bytes);
  if (status.is_error()) {
    ERROR("Couldn't find fit\n");
    return status.take_error();
  }
  const size_t start = status->start;
  size_t length = status->length;
  LOG("Found space in GPT - OK %zu @ %zu\n", length, start);

  if (optional_reserve_bytes) {
    // If we can fulfill the requested size, and we still have space for the
    // optional reserve section, then we should shorten the amount of blocks
    // we're asking for.
    //
    // This isn't necessary, but it allows growing the GPT later, if necessary.
    const size_t optional_reserve_blocks = optional_reserve_bytes / block_info_.block_size;
    if (length - optional_reserve_bytes > (minimum_size_bytes / block_info_.block_size)) {
      LOG("Space for reserve - OK\n");
      length -= optional_reserve_blocks;
    }
  } else {
    length = fbl::round_up(minimum_size_bytes, block_info_.block_size) / block_info_.block_size;
  }
  LOG("Final space in GPT - OK %zu @ %zu\n", length, start);

  auto status_or_guid = CreateGptPartition(name, type, start, length);
  if (status_or_guid.is_error()) {
    return status_or_guid.take_error();
  }
  LOG("Added partition, waiting for bind\n");

  auto status_or_part = OpenBlockPartition(devfs_root_, status_or_guid.value(), type, ZX_SEC(15));
  if (status_or_part.is_error()) {
    ERROR("Added partition, waiting for bind - NOT FOUND\n");
    return status_or_part.take_error();
  }

  LOG("Added partition, waiting for bind - OK\n");
  return zx::ok(new BlockPartitionClient(std::move(status_or_part.value())));
}

zx::status<GptDevicePartitioner::FindPartitionResult> GptDevicePartitioner::FindPartition(
    FilterCallback filter) const {
  for (uint32_t i = 0; i < gpt::kPartitionCount; i++) {
    gpt_partition_t* p = gpt_->GetPartition(i);
    if (!p) {
      continue;
    }

    if (filter(*p)) {
      LOG("Found partition in GPT, partition %u\n", i);
      auto status = OpenBlockPartition(devfs_root_, Uuid(p->guid), Uuid(p->type), ZX_SEC(5));
      if (status.is_error()) {
        ERROR("Couldn't open partition\n");
        return status.take_error();
      }
      auto part = std::make_unique<BlockPartitionClient>(std::move(status.value()));
      return zx::ok(FindPartitionResult{std::move(part), p});
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

zx::status<> GptDevicePartitioner::WipePartitions(FilterCallback filter) const {
  bool modify = false;
  for (uint32_t i = 0; i < gpt::kPartitionCount; i++) {
    const gpt_partition_t* p = gpt_->GetPartition(i);
    if (!p) {
      continue;
    }
    if (!filter(*p)) {
      continue;
    }

    modify = true;

    // Ignore the return status; wiping is a best-effort approach anyway.
    WipeBlockPartition(devfs_root_, Uuid(p->guid), Uuid(p->type)).status_value();

    if (gpt_->RemovePartition(p->guid) != ZX_OK) {
      ERROR("Warning: Could not remove partition\n");
    } else {
      // If we successfully clear the partition, then all subsequent
      // partitions get shifted down. If we just deleted partition 'i',
      // we now need to look at partition 'i' again, since it's now
      // occupied by what was in 'i+1'.
      i--;
    }
  }
  if (modify) {
    gpt_->Sync();
    LOG("Immediate reboot strongly recommended\n");
  }
  __UNUSED auto unused = RebindGptDriver(svc_root_, Channel()).status_value();
  return zx::ok();
}

zx::status<> GptDevicePartitioner::WipeFvm() const { return WipePartitions(IsFvmPartition); }

zx::status<> GptDevicePartitioner::WipePartitionTables() const {
  return WipePartitions([](const gpt_partition_t&) { return true; });
}

}  // namespace paver
