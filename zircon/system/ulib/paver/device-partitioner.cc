// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device-partitioner.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>

#include <chromeos-disk-setup/chromeos-disk-setup.h>
#undef GUID_LEN
#include <fbl/auto_call.h>
#include <fbl/function.h>
#include <fbl/string_buffer.h>
#include <fs-management/fvm.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <fuchsia/hardware/block/partition/llcpp/fidl.h>
#include <fuchsia/hardware/skipblock/llcpp/fidl.h>
#include <gpt/cros.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/fdio.h>
#include <zircon/status.h>
#include <zxcrypt/volume.h>

#include <utility>
#include <string>

#include "pave-logging.h"
#include "pave-utils.h"

namespace paver {

namespace {

namespace block = ::llcpp::fuchsia::hardware::block;
namespace partition = ::llcpp::fuchsia::hardware::block::partition;
namespace skipblock = ::llcpp::fuchsia::hardware::skipblock;

constexpr char kEfiName[] = "EFI Gigaboot";
constexpr char kGptDriverName[] = "/boot/driver/gpt.so";
constexpr char kFvmPartitionName[] = "fvm";
constexpr char kZirconAName[] = "ZIRCON-A";
constexpr char kZirconBName[] = "ZIRCON-B";
constexpr char kZirconRName[] = "ZIRCON-R";

bool KernelFilterCallback(const gpt_partition_t& part, const uint8_t kern_type[GPT_GUID_LEN],
                          fbl::StringPiece partition_name) {
  char cstring_name[GPT_NAME_LEN];
  utf16_to_cstring(cstring_name, reinterpret_cast<const uint16_t*>(part.name), GPT_NAME_LEN);
  return memcmp(part.type, kern_type, GPT_GUID_LEN) == 0 &&
         strncmp(cstring_name, partition_name.data(), partition_name.length()) == 0;
}

bool IsFvmPartition(const gpt_partition_t& part) {
  const uint8_t partition_type[GPT_GUID_LEN] = GUID_FVM_VALUE;
  return memcmp(part.type, partition_type, GPT_GUID_LEN) == 0;
}

bool IsGigabootPartition(const gpt_partition_t& part) {
  const uint8_t efi_type[GPT_GUID_LEN] = GUID_EFI_VALUE;
  char cstring_name[GPT_NAME_LEN];
  utf16_to_cstring(cstring_name, reinterpret_cast<const uint16_t*>(part.name), GPT_NAME_LEN);
  // Disk-paved EFI: Identified by "EFI Gigaboot" label.
  const bool gigaboot_efi = strncmp(cstring_name, kEfiName, strlen(kEfiName)) == 0;
  return memcmp(part.type, efi_type, GPT_GUID_LEN) == 0 && gigaboot_efi;
}

constexpr size_t ReservedHeaderBlocks(size_t blk_size) {
  constexpr size_t kReservedEntryBlocks = (16 * 1024);
  return (kReservedEntryBlocks + 2 * blk_size) / blk_size;
}

// Helper function to auto-deduce type.
template <typename T>
fbl::unique_ptr<T> WrapUnique(T* ptr) {
  return fbl::unique_ptr<T>(ptr);
}

zx_status_t OpenPartition(const fbl::unique_fd& devfs_root, const char* path,
                          fbl::Function<bool(const fbl::unique_fd&)> should_filter_file,
                          zx_duration_t timeout, fbl::unique_fd* out_partition) {
  ZX_ASSERT(path != nullptr);

  struct CallbackInfo {
    fbl::unique_fd* out_partition;
    fbl::Function<bool(const fbl::unique_fd&)> should_filter_file;
  };

  CallbackInfo info = {
      .out_partition = out_partition,
      .should_filter_file = std::move(should_filter_file),
  };

  auto cb = [](int dirfd, int event, const char* filename, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
      return ZX_OK;
    }
    if ((strcmp(filename, ".") == 0) || strcmp(filename, "..") == 0) {
      return ZX_OK;
    }
    fbl::unique_fd devfd(openat(dirfd, filename, O_RDWR));
    if (!devfd) {
      return ZX_OK;
    }
    auto info = static_cast<CallbackInfo*>(cookie);
    if (info->should_filter_file(devfd)) {
      return ZX_OK;
    }
    if (info->out_partition) {
      *(info->out_partition) = std::move(devfd);
    }
    return ZX_ERR_STOP;
  };

  fbl::unique_fd dir_fd(openat(devfs_root.get(), path, O_RDONLY));
  if (!dir_fd) {
    return ZX_ERR_IO;
  }
  DIR* dir = fdopendir(dir_fd.release());
  if (dir == nullptr) {
    return ZX_ERR_IO;
  }
  const auto closer = fbl::MakeAutoCall([&dir]() { closedir(dir); });

  zx_time_t deadline = zx_deadline_after(timeout);
  if (fdio_watch_directory(dirfd(dir), cb, deadline, &info) != ZX_ERR_STOP) {
    return ZX_ERR_NOT_FOUND;
  }
  return ZX_OK;
}

constexpr char kBlockDevPath[] = "class/block/";

zx_status_t OpenBlockPartition(const fbl::unique_fd& devfs_root, const uint8_t* unique_guid,
                               const uint8_t* type_guid, zx_duration_t timeout,
                               fbl::unique_fd* out_fd) {
  ZX_ASSERT(unique_guid || type_guid);

  auto cb = [&](const fbl::unique_fd& fd) {
    fzl::UnownedFdioCaller caller(fd.get());
    if (type_guid) {
      auto result = partition::Partition::Call::GetTypeGuid(caller.channel());
      if (!result.ok()) {
        return true;
      }
      auto& response = result.value();
      if (response.status != ZX_OK ||
          memcmp(response.guid->value.data(), type_guid, partition::GUID_LENGTH) != 0) {
        return true;
      }
    }
    if (unique_guid) {
      auto result = partition::Partition::Call::GetInstanceGuid(caller.channel());
      if (!result.ok()) {
        return true;
      }
      const auto& response = result.value();
      if (response.status != ZX_OK ||
          memcmp(response.guid->value.data(), unique_guid, partition::GUID_LENGTH) != 0) {
        return true;
      }
    }
    return false;
  };

  return OpenPartition(devfs_root, kBlockDevPath, cb, timeout, out_fd);
}

constexpr char kSkipBlockDevPath[] = "class/skip-block/";

zx_status_t OpenSkipBlockPartition(const fbl::unique_fd& devfs_root, const uint8_t* type_guid,
                                   zx_duration_t timeout, fbl::unique_fd* out_fd) {
  ZX_ASSERT(type_guid);

  auto cb = [&](const fbl::unique_fd& fd) {
    fzl::UnownedFdioCaller caller(fd.get());

    auto result = skipblock::SkipBlock::Call::GetPartitionInfo(caller.channel());
    if (!result.ok()) {
      return true;
    }
    const auto& response = result.value();
    if (response.status != ZX_OK || memcmp(response.partition_info.partition_guid.data(), type_guid,
                                           skipblock::GUID_LEN) != 0) {
      return true;
    }
    return false;
  };

  return OpenPartition(devfs_root, kSkipBlockDevPath, cb, timeout, out_fd);
}

bool HasSkipBlockDevice(const fbl::unique_fd& devfs_root) {
  // Our proxy for detected a skip-block device is by checking for the
  // existence of a device enumerated under the skip-block class.
  const uint8_t type[GPT_GUID_LEN] = GUID_ZIRCON_A_VALUE;
  return OpenSkipBlockPartition(devfs_root, type, ZX_SEC(1), nullptr) == ZX_OK;
}

// Attempts to open and overwrite the first block of the underlying
// partition. Does not rebind partition drivers.
//
// At most one of |unique_guid| and |type_guid| may be nullptr.
zx_status_t WipeBlockPartition(const fbl::unique_fd& devfs_root, const uint8_t* unique_guid,
                               const uint8_t* type_guid) {
  zx_status_t status = ZX_OK;
  fbl::unique_fd fd;
  if ((status = OpenBlockPartition(devfs_root, unique_guid, type_guid, ZX_SEC(3), &fd)) != ZX_OK) {
    ERROR("Warning: Could not open partition to wipe: %s\n", zx_status_get_string(status));
    return status;
  }

  fzl::UnownedFdioCaller caller(fd.get());
  auto result = block::Block::Call::GetInfo(caller.channel());
  if (!result.ok()) {
    ERROR("Warning: Could not acquire block info: %s\n", zx_status_get_string(result.status()));
    return result.status();
  }
  const auto& response = result.value();
  if (response.status != ZX_OK) {
    ERROR("Warning: Could not acquire block info: %s\n", zx_status_get_string(response.status));
    return response.status;
  }

  // Overwrite the first block to (hackily) ensure the destroyed partition
  // doesn't "reappear" in place.
  auto block_size = response.info->block_size;
  char buf[block_size];
  memset(buf, 0, block_size);

  if (pwrite(fd.get(), buf, block_size, 0) != block_size) {
    ERROR("Warning: Could not write to block device: %s\n", strerror(errno));
    return ZX_ERR_IO;
  }

  if ((status = FlushBlockDevice(fd)) != ZX_OK) {
    ERROR("Warning: Failed to synchronize block device: %s\n", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

}  // namespace

const char* PartitionName(Partition type) {
  switch (type) {
    case Partition::kBootloader:
      return "Bootloader";
    case Partition::kZirconA:
      return "Zircon A";
    case Partition::kZirconB:
      return "Zircon B";
    case Partition::kZirconR:
      return "Zircon R";
    case Partition::kVbMetaA:
      return "VBMeta A";
    case Partition::kVbMetaB:
      return "VBMeta B";
    case Partition::kVbMetaR:
      return "VBMeta R";
    case Partition::kFuchsiaVolumeManager:
      return "Fuchsia Volume Manager";
    default:
      return "Unknown";
  }
}

fbl::unique_ptr<DevicePartitioner> DevicePartitioner::Create(fbl::unique_fd devfs_root, Arch arch,
                                                             zx::channel block_device) {
  std::optional<fbl::unique_fd> block_dev;
  std::optional<fbl::unique_fd> block_dev_dup;
  if (block_device) {
    int fd;
    zx_status_t status = fdio_fd_create(block_device.release(), &fd);
    if (status != ZX_OK) {
      ERROR(
          "Unable to create fd from block_device channel. Does it implement fuchsia.io.Node?: %s\n",
          zx_status_get_string(status));
      return nullptr;
    }
    block_dev.emplace(fd);
    block_dev_dup = block_dev->duplicate();
  }
  fbl::unique_ptr<DevicePartitioner> device_partitioner;
  if ((SkipBlockDevicePartitioner::Initialize(devfs_root.duplicate(), &device_partitioner) ==
       ZX_OK) ||
      (CrosDevicePartitioner::Initialize(devfs_root.duplicate(), arch, std::move(block_dev_dup),
                                         &device_partitioner) == ZX_OK) ||
      (EfiDevicePartitioner::Initialize(devfs_root.duplicate(), arch, std::move(block_dev),
                                        &device_partitioner) == ZX_OK) ||
      (FixedDevicePartitioner::Initialize(std::move(devfs_root), &device_partitioner) == ZX_OK)) {
    return device_partitioner;
  }
  return nullptr;
}

/*====================================================*
 *                  GPT Common                        *
 *====================================================*/

bool GptDevicePartitioner::FindGptDevices(const fbl::unique_fd& devfs_root, GptDevices* out) {
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
    fzl::FdioCaller caller(std::move(fd));

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
    auto result2 = ::llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(caller.channel());
    if (result2.status() != ZX_OK) {
      continue;
    }
    const auto& response2 = result2.value();
    if (response2.status != ZX_OK) {
      continue;
    }

    std::string path_str(response2.path.data(), static_cast<size_t>(response2.path.size()));

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

zx_status_t GptDevicePartitioner::InitializeProvidedGptDevice(
    fbl::unique_fd devfs_root, fbl::unique_fd gpt_device,
    fbl::unique_ptr<GptDevicePartitioner>* gpt_out) {

  fzl::UnownedFdioCaller caller(gpt_device.get());
  auto result = block::Block::Call::GetInfo(caller.channel());
  if (!result.ok()) {
    ERROR("Warning: Could not acquire GPT block info: %s\n", zx_status_get_string(result.status()));
    return result.status();
  }
  const auto& response = result.value();
  if (response.status != ZX_OK) {
    ERROR("Warning: Could not acquire GPT block info: %s\n", zx_status_get_string(response.status));
    return response.status;
  }

  fbl::unique_ptr<GptDevice> gpt;
  if (GptDevice::Create(gpt_device.get(), response.info->block_size, response.info->block_count,
                        &gpt) != ZX_OK) {
    ERROR("Failed to get GPT info\n");
    return ZX_ERR_BAD_STATE;
  }

  if (!gpt->Valid()) {
    ERROR("Located GPT is invalid; Attempting to initialize\n");
    if (gpt->RemoveAllPartitions() != ZX_OK) {
      ERROR("Failed to create empty GPT\n");
      return ZX_ERR_BAD_STATE;
    }
    if (gpt->Sync() != ZX_OK) {
      ERROR("Failed to sync empty GPT\n");
      return ZX_ERR_BAD_STATE;
    }
    // Try to rebind the GPT, in case a prior GPT driver was actually
    // up and running.
    auto result = block::Block::Call::RebindDevice(caller.channel());
    if (!result.ok() || result.value().status != ZX_OK) {
      ERROR("Failed to re-read GPT\n");
      return ZX_ERR_BAD_STATE;
    }

    // Manually re-bind the GPT driver, since it is almost certainly
    // too late to be noticed by the block watcher.
    auto result2 = ::llcpp::fuchsia::device::Controller::Call::Bind(
        caller.channel(), fidl::StringView(strlen(kGptDriverName), kGptDriverName));
    if (result2.status() != ZX_OK || result2.value().status != ZX_OK) {
      ERROR("Failed to bind GPT\n");
      return ZX_ERR_BAD_STATE;
    }
  }

  *gpt_out = WrapUnique(new GptDevicePartitioner(
      devfs_root.duplicate(), std::move(gpt_device), std::move(gpt), *(response.info)));
  return ZX_OK;
}

zx_status_t GptDevicePartitioner::InitializeGpt(fbl::unique_fd devfs_root, Arch arch,
                                                std::optional<fbl::unique_fd> block_device,
                                                fbl::unique_ptr<GptDevicePartitioner>* gpt_out) {
  if (arch != Arch::kX64) {
    return ZX_ERR_NOT_FOUND;
  }

  if (block_device) {
    return InitializeProvidedGptDevice(std::move(devfs_root), *std::move(block_device), gpt_out);
  }

  GptDevices gpt_devices;
  if (!FindGptDevices(devfs_root, &gpt_devices)) {
    ERROR("Failed to find GPT\n");
    return ZX_ERR_NOT_FOUND;
  }

  std::unique_ptr<GptDevicePartitioner> gpt_partitioner;
  for (auto& [_, gpt_device] : gpt_devices) {
    fzl::UnownedFdioCaller caller(gpt_device.get());
    auto result = block::Block::Call::GetInfo(caller.channel());
    if (!result.ok()) {
      ERROR("Warning: Could not acquire GPT block info: %s\n",
            zx_status_get_string(result.status()));
      return result.status();
    }
    const auto& response = result.value();
    if (response.status != ZX_OK) {
      ERROR("Warning: Could not acquire GPT block info: %s\n",
            zx_status_get_string(response.status));
      return response.status;
    }

    fbl::unique_ptr<GptDevice> gpt;
    if (GptDevice::Create(gpt_device.get(), response.info->block_size,
                          response.info->block_count, &gpt) != ZX_OK) {
      ERROR("Failed to get GPT info\n");
      return ZX_ERR_BAD_STATE;
    }

    if (!gpt->Valid()) {
      continue;
    }

    auto partitioner = WrapUnique(new GptDevicePartitioner(
        devfs_root.duplicate(), std::move(gpt_device), std::move(gpt), *(response.info)));

    if (partitioner->FindPartition(IsFvmPartition, nullptr, nullptr) != ZX_OK) {
      continue;
    }

    if (gpt_partitioner) {
      ERROR("Found multiple block devices with valid GPTs. Unsuppported.\n");
      return ZX_ERR_NOT_SUPPORTED;
    }
    gpt_partitioner = std::move(partitioner);
  }

  if (gpt_partitioner) {
    *gpt_out = std::move(gpt_partitioner);
    return ZX_OK;
  }

  ERROR(
      "Unable to find a valid GPT on this device with the expected partitions. "
      "Please run *one* of the following command(s):\n");

  for (const auto& [gpt_path, _] : gpt_devices) {
    ERROR("install-disk-image init-gpt --block-device %s\n", gpt_path.c_str());
  }

  return ZX_ERR_NOT_FOUND;
}

struct PartitionPosition {
  size_t start;   // Block, inclusive
  size_t length;  // In Blocks
};

zx_status_t GptDevicePartitioner::FindFirstFit(size_t bytes_requested, size_t* start_out,
                                               size_t* length_out) const {
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
    return static_cast<int>(s1 - s2);
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
      return ZX_ERR_IO;
    }
    const size_t free_blocks = partitions[i + 1].start - next;
    LOG("    There are %zu free blocks (%zu requested)\n", free_blocks, blocks_requested);
    if (free_blocks >= blocks_requested) {
      *start_out = next;
      *length_out = free_blocks;
      return ZX_OK;
    }
  }
  ERROR("No GPT space found\n");
  return ZX_ERR_NO_RESOURCES;
}

zx_status_t GptDevicePartitioner::CreateGptPartition(const char* name, uint8_t* type,
                                                     uint64_t offset, uint64_t blocks,
                                                     uint8_t* out_guid) const {
  zx_cprng_draw(out_guid, GPT_GUID_LEN);

  zx_status_t status;
  if ((status = gpt_->AddPartition(name, type, out_guid, offset, blocks, 0)) != ZX_OK) {
    ERROR("Failed to add partition\n");
    return ZX_ERR_IO;
  }
  if ((status = gpt_->Sync()) != ZX_OK) {
    ERROR("Failed to sync GPT\n");
    return ZX_ERR_IO;
  }
  if ((status = gpt_->ClearPartition(offset, 1)) != ZX_OK) {
    ERROR("Failed to clear first block of new partition\n");
    return status;
  }
  auto result = block::Block::Call::RebindDevice(Channel());
  if (!result.ok()) {
    ERROR("Failed to rebind GPT\n");
    return result.status();
  }
  const auto& response = result.value();
  if (response.status != ZX_OK) {
    ERROR("Failed to rebind GPT\n");
    return response.status;
  }
  // Manually re-bind the GPT driver, since it is almost certainly
  // too late to be noticed by the block watcher.
  auto result2 = ::llcpp::fuchsia::device::Controller::Call::Bind(
      Channel(), fidl::StringView(strlen(kGptDriverName), kGptDriverName));
  if (result2.status() != ZX_OK || result2.value().status != ZX_OK) {
    ERROR("Failed to bind GPT\n");
    return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}

zx_status_t GptDevicePartitioner::AddPartition(const char* name, uint8_t* type,
                                               size_t minimum_size_bytes,
                                               size_t optional_reserve_bytes,
                                               fbl::unique_fd* out_fd) const {
  uint64_t start, length;
  zx_status_t status;
  if ((status = FindFirstFit(minimum_size_bytes, &start, &length)) != ZX_OK) {
    ERROR("Couldn't find fit\n");
    return status;
  }
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

  uint8_t guid[GPT_GUID_LEN];
  if ((status = CreateGptPartition(name, type, start, length, guid)) != ZX_OK) {
    return status;
  }
  LOG("Added partition, waiting for bind\n");

  if ((status = OpenBlockPartition(devfs_root_, guid, type, ZX_SEC(15), out_fd)) != ZX_OK) {
    ERROR("Added partition, waiting for bind - NOT FOUND\n");
    return status;
  }
  LOG("Added partition, waiting for bind - OK\n");
  return ZX_OK;
}

zx_status_t GptDevicePartitioner::FindPartition(FilterCallback filter, fbl::unique_fd* out_fd,
                                                gpt_partition_t** out) const {
  for (uint32_t i = 0; i < gpt::kPartitionCount; i++) {
    gpt_partition_t* p = gpt_->GetPartition(i);
    if (!p) {
      continue;
    }

    if (filter(*p)) {
      LOG("Found partition in GPT, partition %u\n", i);
      if (out) {
        *out = p;
      }
      if (out_fd) {
        zx_status_t status;
        status = OpenBlockPartition(devfs_root_, p->guid, p->type, ZX_SEC(5), out_fd);
        if (status != ZX_OK) {
          ERROR("Couldn't open partition\n");
          return status;
        }
      }
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

zx_status_t GptDevicePartitioner::WipeFvm() const {
  bool modify = false;
  for (uint32_t i = 0; i < gpt::kPartitionCount; i++) {
    const gpt_partition_t* p = gpt_->GetPartition(i);
    if (!p) {
      continue;
    }
    if (!IsFvmPartition(*p)) {
      continue;
    }

    modify = true;

    // Ignore the return status; wiping is a best-effort approach anyway.
    WipeBlockPartition(devfs_root_, p->guid, p->type);

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
  block::Block::Call::RebindDevice(Channel());
  return ZX_OK;
}

/*====================================================*
 *                 EFI SPECIFIC                       *
 *====================================================*/

zx_status_t EfiDevicePartitioner::Initialize(fbl::unique_fd devfs_root, Arch arch,
                                             std::optional<fbl::unique_fd> block_device,
                                             fbl::unique_ptr<DevicePartitioner>* partitioner) {
  fbl::unique_ptr<GptDevicePartitioner> gpt;
  zx_status_t status = GptDevicePartitioner::InitializeGpt(std::move(devfs_root), arch,
                                                           std::move(block_device), &gpt);
  if (status != ZX_OK) {
    return status;
  }
  if (is_cros(gpt->GetGpt())) {
    ERROR("Use CrOS Device Partitioner.");
    return ZX_ERR_NOT_SUPPORTED;
  }

  LOG("Successfully initialized EFI Device Partitioner\n");
  *partitioner = WrapUnique(new EfiDevicePartitioner(std::move(gpt)));
  return ZX_OK;
}

zx_status_t EfiDevicePartitioner::AddPartition(Partition partition_type,
                                               fbl::unique_fd* out_fd) const {
  const char* name;
  uint8_t type[GPT_GUID_LEN];
  size_t minimum_size_bytes = 0;
  size_t optional_reserve_bytes = 0;

  switch (partition_type) {
    case Partition::kBootloader: {
      const uint8_t efi_type[GPT_GUID_LEN] = GUID_EFI_VALUE;
      memcpy(type, efi_type, GPT_GUID_LEN);
      minimum_size_bytes = 20LU * (1 << 20);
      name = kEfiName;
      break;
    }
    case Partition::kZirconA: {
      const uint8_t zircon_a_type[GPT_GUID_LEN] = GUID_ZIRCON_A_VALUE;
      memcpy(type, zircon_a_type, GPT_GUID_LEN);
      minimum_size_bytes = 16LU * (1 << 20);
      name = kZirconAName;
      break;
    }
    case Partition::kZirconB: {
      const uint8_t zircon_b_type[GPT_GUID_LEN] = GUID_ZIRCON_B_VALUE;
      memcpy(type, zircon_b_type, GPT_GUID_LEN);
      minimum_size_bytes = 16LU * (1 << 20);
      name = kZirconBName;
      break;
    }
    case Partition::kZirconR: {
      const uint8_t zircon_r_type[GPT_GUID_LEN] = GUID_ZIRCON_R_VALUE;
      memcpy(type, zircon_r_type, GPT_GUID_LEN);
      minimum_size_bytes = 24LU * (1 << 20);
      name = kZirconRName;
      break;
    }
    case Partition::kFuchsiaVolumeManager: {
      const uint8_t fvm_type[GPT_GUID_LEN] = GUID_FVM_VALUE;
      memcpy(type, fvm_type, GPT_GUID_LEN);
      minimum_size_bytes = 8LU * (1 << 30);
      name = kFvmPartitionName;
      break;
    }
    default:
      ERROR("EFI partitioner cannot add unknown partition type\n");
      return ZX_ERR_NOT_SUPPORTED;
  }

  return gpt_->AddPartition(name, type, minimum_size_bytes, optional_reserve_bytes, out_fd);
}

zx_status_t EfiDevicePartitioner::FindPartition(Partition partition_type,
                                                fbl::unique_fd* out_fd) const {
  switch (partition_type) {
    case Partition::kBootloader: {
      return gpt_->FindPartition(IsGigabootPartition, out_fd);
    }
    case Partition::kZirconA: {
      const auto filter = [](const gpt_partition_t& part) {
        const uint8_t guid[GPT_GUID_LEN] = GUID_ZIRCON_A_VALUE;
        return KernelFilterCallback(part, guid, kZirconAName);
      };
      return gpt_->FindPartition(filter, out_fd);
    }
    case Partition::kZirconB: {
      const auto filter = [](const gpt_partition_t& part) {
        const uint8_t guid[GPT_GUID_LEN] = GUID_ZIRCON_B_VALUE;
        return KernelFilterCallback(part, guid, kZirconBName);
      };
      return gpt_->FindPartition(filter, out_fd);
    }
    case Partition::kZirconR: {
      const auto filter = [](const gpt_partition_t& part) {
        const uint8_t guid[GPT_GUID_LEN] = GUID_ZIRCON_R_VALUE;
        return KernelFilterCallback(part, guid, kZirconRName);
      };
      return gpt_->FindPartition(filter, out_fd);
    }
    case Partition::kFuchsiaVolumeManager:
      return gpt_->FindPartition(IsFvmPartition, out_fd);

    default:
      ERROR("EFI partitioner cannot find unknown partition type\n");
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t EfiDevicePartitioner::WipeFvm() const { return gpt_->WipeFvm(); }

zx_status_t EfiDevicePartitioner::GetBlockSize(const fbl::unique_fd& device_fd,
                                               uint32_t* block_size) const {
  block::BlockInfo info;
  zx_status_t status = gpt_->GetBlockInfo(&info);
  if (status == ZX_OK) {
    *block_size = info.block_size;
  }
  return status;
}

/*====================================================*
 *                CROS SPECIFIC                       *
 *====================================================*/

zx_status_t CrosDevicePartitioner::Initialize(fbl::unique_fd devfs_root, Arch arch,
                                              std::optional<fbl::unique_fd> block_device,
                                              fbl::unique_ptr<DevicePartitioner>* partitioner) {
  fbl::unique_ptr<GptDevicePartitioner> gpt_partitioner;
  zx_status_t status = GptDevicePartitioner::InitializeGpt(
      std::move(devfs_root), arch, std::move(block_device), &gpt_partitioner);
  if (status != ZX_OK) {
    return status;
  }

  GptDevice* gpt = gpt_partitioner->GetGpt();
  if (!is_cros(gpt)) {
    return ZX_ERR_NOT_FOUND;
  }

  block::BlockInfo info;
  gpt_partitioner->GetBlockInfo(&info);

  if (!is_ready_to_pave(gpt, reinterpret_cast<fuchsia_hardware_block_BlockInfo*>(&info),
                        SZ_ZX_PART)) {
    status = config_cros_for_fuchsia(
        gpt, reinterpret_cast<fuchsia_hardware_block_BlockInfo*>(&info), SZ_ZX_PART);
    if (status != ZX_OK) {
      ERROR("Failed to configure CrOS for Fuchsia.\n");
      return status;
    }
    if ((status = gpt->Sync()) != ZX_OK) {
      ERROR("Failed to sync CrOS for Fuchsia.\n");
      return status;
    }
    block::Block::Call::RebindDevice(gpt_partitioner->Channel());
  }

  LOG("Successfully initialized CrOS Device Partitioner\n");
  *partitioner = WrapUnique(new CrosDevicePartitioner(std::move(gpt_partitioner)));
  return ZX_OK;
}

zx_status_t CrosDevicePartitioner::AddPartition(Partition partition_type,
                                                fbl::unique_fd* out_fd) const {
  const char* name;
  uint8_t type[GPT_GUID_LEN];
  size_t minimum_size_bytes = 0;
  size_t optional_reserve_bytes = 0;

  switch (partition_type) {
    case Partition::kZirconA: {
      const uint8_t kernc_type[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
      memcpy(type, kernc_type, GPT_GUID_LEN);
      minimum_size_bytes = 64LU * (1 << 20);
      name = kZirconAName;
      break;
    }
    case Partition::kZirconR: {
      const uint8_t zircon_r_type[GPT_GUID_LEN] = GUID_ZIRCON_R_VALUE;
      memcpy(type, zircon_r_type, GPT_GUID_LEN);
      minimum_size_bytes = 24LU * (1 << 20);
      name = kZirconRName;
      break;
    }
    case Partition::kFuchsiaVolumeManager: {
      const uint8_t fvm_type[GPT_GUID_LEN] = GUID_FVM_VALUE;
      memcpy(type, fvm_type, GPT_GUID_LEN);
      minimum_size_bytes = 8LU * (1 << 30);
      name = kFvmPartitionName;
      break;
    }
    default:
      ERROR("Cros partitioner cannot add unknown partition type\n");
      return ZX_ERR_NOT_SUPPORTED;
  }
  return gpt_->AddPartition(name, type, minimum_size_bytes, optional_reserve_bytes, out_fd);
}

zx_status_t CrosDevicePartitioner::FindPartition(Partition partition_type,
                                                 fbl::unique_fd* out_fd) const {
  switch (partition_type) {
    case Partition::kZirconA: {
      const auto filter = [](const gpt_partition_t& part) {
        const uint8_t guid[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
        return KernelFilterCallback(part, guid, kZirconAName);
      };
      return gpt_->FindPartition(filter, out_fd);
    }
    case Partition::kZirconR: {
      const auto filter = [](const gpt_partition_t& part) {
        const uint8_t guid[GPT_GUID_LEN] = GUID_ZIRCON_R_VALUE;
        return KernelFilterCallback(part, guid, kZirconRName);
      };
      return gpt_->FindPartition(filter, out_fd);
    }
    case Partition::kFuchsiaVolumeManager:
      return gpt_->FindPartition(IsFvmPartition, out_fd);

    default:
      ERROR("Cros partitioner cannot find unknown partition type\n");
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t CrosDevicePartitioner::FinalizePartition(Partition partition_type) const {
  // Special partition finalization is only necessary for Zircon partitions.
  if (partition_type != Partition::kZirconA) {
    return ZX_OK;
  }

  uint8_t top_priority = 0;

  const uint8_t kern_type[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
  constexpr char kPrefix[] = "ZIRCON-";
  uint16_t zircon_prefix[strlen(kPrefix) * 2];
  cstring_to_utf16(&zircon_prefix[0], kPrefix, strlen(kPrefix));

  for (uint32_t i = 0; i < gpt::kPartitionCount; ++i) {
    const gpt_partition_t* part = gpt_->GetGpt()->GetPartition(i);
    if (part == NULL) {
      continue;
    }
    if (memcmp(part->type, kern_type, GPT_GUID_LEN)) {
      continue;
    }
    if (memcmp(part->name, zircon_prefix, strlen(kPrefix) * 2)) {
      const uint8_t priority = gpt_cros_attr_get_priority(part->flags);
      if (priority > top_priority) {
        top_priority = priority;
      }
    }
  }

  const auto filter_zircona = [](const gpt_partition_t& part) {
    const uint8_t guid[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
    return KernelFilterCallback(part, guid, kZirconAName);
  };
  zx_status_t status;
  gpt_partition_t* partition;
  if ((status = gpt_->FindPartition(filter_zircona, nullptr, &partition)) != ZX_OK) {
    ERROR("Cannot find %s partition\n", kZirconAName);
    return status;
  }

  // Priority for Zircon A set to higher priority than all other kernels.
  if (top_priority == UINT8_MAX) {
    ERROR("Cannot set CrOS partition priority higher than other kernels\n");
    return ZX_ERR_OUT_OF_RANGE;
  }

  // TODO(raggi): when other (B/R) partitions are paved, set their priority
  // appropriately as well.

  if (gpt_cros_attr_set_priority(&partition->flags, ++top_priority) != 0) {
    ERROR("Cannot set CrOS partition priority for ZIRCON-A\n");
    return ZX_ERR_OUT_OF_RANGE;
  }
  // Successful set to 'true' to encourage the bootloader to
  // use this partition.
  gpt_cros_attr_set_successful(&partition->flags, true);
  // Maximize the number of attempts to boot this partition before
  // we fall back to a different kernel.
  if (gpt_cros_attr_set_tries(&partition->flags, 15) != 0) {
    ERROR("Cannot set CrOS partition 'tries' for KERN-C\n");
    return ZX_ERR_OUT_OF_RANGE;
  }
  if ((status = gpt_->GetGpt()->Sync()) == ZX_OK) {
    ERROR("Failed to sync CrOS partition 'tries' for KERN-C.\n");
    return status;
  }

  return ZX_OK;
}

zx_status_t CrosDevicePartitioner::WipeFvm() const { return gpt_->WipeFvm(); }

zx_status_t CrosDevicePartitioner::GetBlockSize(const fbl::unique_fd& device_fd,
                                                uint32_t* block_size) const {
  block::BlockInfo info;
  zx_status_t status = gpt_->GetBlockInfo(&info);
  if (status == ZX_OK) {
    *block_size = info.block_size;
  }
  return status;
}

/*====================================================*
 *               FIXED PARTITION MAP                  *
 *====================================================*/

zx_status_t FixedDevicePartitioner::Initialize(fbl::unique_fd devfs_root,
                                               fbl::unique_ptr<DevicePartitioner>* partitioner) {
  if (HasSkipBlockDevice(devfs_root)) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  LOG("Successfully initialized FixedDevicePartitioner Device Partitioner\n");
  *partitioner = WrapUnique(new FixedDevicePartitioner(std::move(devfs_root)));
  return ZX_OK;
}

zx_status_t FixedDevicePartitioner::AddPartition(Partition partition_type,
                                                 fbl::unique_fd* out_fd) const {
  ERROR("Cannot add partitions to a fixed-map partition device\n");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FixedDevicePartitioner::FindPartition(Partition partition_type,
                                                  fbl::unique_fd* out_fd) const {
  uint8_t type[GPT_GUID_LEN];

  switch (partition_type) {
    case Partition::kZirconA: {
      const uint8_t zircon_a_type[GPT_GUID_LEN] = GUID_ZIRCON_A_VALUE;
      memcpy(type, zircon_a_type, GPT_GUID_LEN);
      break;
    }
    case Partition::kZirconB: {
      const uint8_t zircon_b_type[GPT_GUID_LEN] = GUID_ZIRCON_B_VALUE;
      memcpy(type, zircon_b_type, GPT_GUID_LEN);
      break;
    }
    case Partition::kZirconR: {
      const uint8_t zircon_r_type[GPT_GUID_LEN] = GUID_ZIRCON_R_VALUE;
      memcpy(type, zircon_r_type, GPT_GUID_LEN);
      break;
    }
    case Partition::kVbMetaA: {
      const uint8_t vbmeta_a_type[GPT_GUID_LEN] = GUID_VBMETA_A_VALUE;
      memcpy(type, vbmeta_a_type, GPT_GUID_LEN);
      break;
    }
    case Partition::kVbMetaB: {
      const uint8_t vbmeta_b_type[GPT_GUID_LEN] = GUID_VBMETA_B_VALUE;
      memcpy(type, vbmeta_b_type, GPT_GUID_LEN);
      break;
    }
    case Partition::kVbMetaR: {
      const uint8_t vbmeta_r_type[GPT_GUID_LEN] = GUID_VBMETA_R_VALUE;
      memcpy(type, vbmeta_r_type, GPT_GUID_LEN);
      break;
    }
    case Partition::kFuchsiaVolumeManager: {
      const uint8_t fvm_type[GPT_GUID_LEN] = GUID_FVM_VALUE;
      memcpy(type, fvm_type, GPT_GUID_LEN);
      break;
    }
    default:
      ERROR("partition_type is invalid!\n");
      return ZX_ERR_NOT_SUPPORTED;
  }

  return OpenBlockPartition(devfs_root_, nullptr, type, ZX_SEC(5), out_fd);
}

zx_status_t FixedDevicePartitioner::WipeFvm() const {
  const uint8_t fvm_type[GPT_GUID_LEN] = GUID_FVM_VALUE;
  zx_status_t status;
  if ((status = WipeBlockPartition(devfs_root_, nullptr, fvm_type)) != ZX_OK) {
    ERROR("Failed to wipe FVM.\n");
  } else {
    LOG("Wiped FVM successfully.\n");
  }
  LOG("Immediate reboot strongly recommended\n");
  return ZX_OK;
}

zx_status_t FixedDevicePartitioner::GetBlockSize(const fbl::unique_fd& device_fd,
                                                 uint32_t* block_size) const {
  fzl::UnownedFdioCaller caller(device_fd.get());

  auto result = block::Block::Call::GetInfo(caller.channel());
  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  if (response.status != ZX_OK) {
    return response.status;
  }
  *block_size = response.info->block_size;
  return ZX_OK;
}

/*====================================================*
 *                SKIP BLOCK SPECIFIC                 *
 *====================================================*/

zx_status_t SkipBlockDevicePartitioner::Initialize(
    fbl::unique_fd devfs_root, fbl::unique_ptr<DevicePartitioner>* partitioner) {
  if (!HasSkipBlockDevice(devfs_root)) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  LOG("Successfully initialized SkipBlockDevicePartitioner Device Partitioner\n");
  *partitioner = WrapUnique(new SkipBlockDevicePartitioner(std::move(devfs_root)));
  return ZX_OK;
}

zx_status_t SkipBlockDevicePartitioner::AddPartition(Partition partition_type,
                                                     fbl::unique_fd* out_fd) const {
  ERROR("Cannot add partitions to a skip-block, fixed partition device\n");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SkipBlockDevicePartitioner::FindPartition(Partition partition_type,
                                                      fbl::unique_fd* out_fd) const {
  uint8_t type[GPT_GUID_LEN];

  switch (partition_type) {
    case Partition::kBootloader: {
      const uint8_t bootloader_type[GPT_GUID_LEN] = GUID_BOOTLOADER_VALUE;
      memcpy(type, bootloader_type, GPT_GUID_LEN);
      break;
    }
    case Partition::kZirconA: {
      const uint8_t zircon_a_type[GPT_GUID_LEN] = GUID_ZIRCON_A_VALUE;
      memcpy(type, zircon_a_type, GPT_GUID_LEN);
      break;
    }
    case Partition::kZirconB: {
      const uint8_t zircon_b_type[GPT_GUID_LEN] = GUID_ZIRCON_B_VALUE;
      memcpy(type, zircon_b_type, GPT_GUID_LEN);
      break;
    }
    case Partition::kZirconR: {
      const uint8_t zircon_r_type[GPT_GUID_LEN] = GUID_ZIRCON_R_VALUE;
      memcpy(type, zircon_r_type, GPT_GUID_LEN);
      break;
    }
    case Partition::kVbMetaA: {
      const uint8_t vbmeta_a_type[GPT_GUID_LEN] = GUID_VBMETA_A_VALUE;
      memcpy(type, vbmeta_a_type, GPT_GUID_LEN);
      break;
    }
    case Partition::kVbMetaB: {
      const uint8_t vbmeta_b_type[GPT_GUID_LEN] = GUID_VBMETA_B_VALUE;
      memcpy(type, vbmeta_b_type, GPT_GUID_LEN);
      break;
    }
    case Partition::kVbMetaR: {
      const uint8_t vbmeta_r_type[GPT_GUID_LEN] = GUID_VBMETA_R_VALUE;
      memcpy(type, vbmeta_r_type, GPT_GUID_LEN);
      break;
    }
    case Partition::kFuchsiaVolumeManager: {
      const uint8_t fvm_type[GPT_GUID_LEN] = GUID_FVM_VALUE;
      memcpy(type, fvm_type, GPT_GUID_LEN);
      // FVM partition is managed so it should expose a normal block device.
      return OpenBlockPartition(devfs_root_, nullptr, type, ZX_SEC(5), out_fd);
    }
    default:
      ERROR("partition_type is invalid!\n");
      return ZX_ERR_NOT_SUPPORTED;
  }

  return OpenSkipBlockPartition(devfs_root_, type, ZX_SEC(5), out_fd);
}

zx_status_t SkipBlockDevicePartitioner::WipeFvm() const {
  const uint8_t fvm_type[GPT_GUID_LEN] = GUID_FVM_VALUE;
  zx_status_t status;
  fbl::unique_fd block_fd;
  status = OpenBlockPartition(devfs_root_, nullptr, fvm_type, ZX_SEC(3), &block_fd);
  if (status != ZX_OK) {
    ERROR("Warning: Could not open partition to wipe: %s\n", zx_status_get_string(status));
    return ZX_OK;
  }

  zx::channel block_dev;
  status = fdio_get_service_handle(block_fd.release(), block_dev.reset_and_get_address());
  if (status != ZX_OK) {
    ERROR("Warning: Could not get block service handle: %s\n", zx_status_get_string(status));
    return status;
  }
  ::llcpp::fuchsia::device::Controller::SyncClient block_client(std::move(block_dev));

  auto result = block_client.GetTopologicalPath();
  if (!result.ok()) {
    ERROR("Warning: Could not get name for partition: %s\n", zx_status_get_string(result.status()));
    return result.status();
  }
  const auto& response = result.value();
  if (response.status != ZX_OK) {
    ERROR("Warning: Could not get name for partition: %s\n", zx_status_get_string(response.status));
    return response.status;
  }

  fbl::StringBuffer<PATH_MAX> name_buffer;
  name_buffer.Append(response.path.data(), static_cast<size_t>(response.path.size()));

  const char* parent = dirname(name_buffer.data());

  fbl::unique_fd parent_fd(open(parent, O_RDONLY));
  if (!parent_fd) {
    ERROR("Warning: Unable to open block parent device.\n");
    return ZX_ERR_IO;
  }

  zx::channel svc;
  status = fdio_get_service_handle(parent_fd.release(), svc.reset_and_get_address());
  if (status != ZX_OK) {
    ERROR("Warning: Could not get service handle: %s\n", zx_status_get_string(status));
    return status;
  }
  block::Ftl::SyncClient client(std::move(svc));
  auto result2 = client.Format();

  return result2.status() == ZX_OK ? result2.value().status : result2.status();
}

zx_status_t SkipBlockDevicePartitioner::GetBlockSize(const fbl::unique_fd& device_fd,
                                                     uint32_t* block_size) const {
  fzl::UnownedFdioCaller caller(device_fd.get());

  // Just in case we are trying to get info about a block-based device.
  //
  // Clone ahead of time; if it is NOT a block device, the connection will be terminated.
  block::Block::SyncClient maybe_block(zx::channel(fdio_service_clone(caller.borrow_channel())));
  auto result = maybe_block.GetInfo();
  if (result.ok()) {
    const auto& response = result.value();
    if (response.status == ZX_OK) {
      *block_size = response.info->block_size;
      return ZX_OK;
    }
  }

  auto result2 = skipblock::SkipBlock::Call::GetPartitionInfo(caller.channel());
  if (result2.status() != ZX_OK) {
    ERROR("Failed to get partition info with status: %d\n", result2.status());
    return result2.status();
  }
  const auto& response = result2.value();
  if (response.status != ZX_OK) {
    ERROR("Failed to get partition info with status: %d\n", response.status);
    return response.status;
  }
  *block_size = static_cast<uint32_t>(response.partition_info.block_size_bytes);

  return ZX_OK;
}

}  // namespace paver
