// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device-partitioner.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/boot/llcpp/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <fuchsia/hardware/block/partition/llcpp/fidl.h>
#include <fuchsia/hardware/skipblock/llcpp/fidl.h>
#include <fuchsia/sysinfo/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/fdio/cpp/caller.h>
#include <libgen.h>
#include <zircon/status.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <chromeos-disk-setup/chromeos-disk-setup.h>
#include <fbl/auto_call.h>
#include <fbl/function.h>
#include <fbl/span.h>
#include <fbl/string_buffer.h>
#include <fs-management/fvm.h>
#include <gpt/cros.h>
#include <soc/aml-common/aml-guid.h>
#include <zxcrypt/volume.h>

#include "lib/fidl/llcpp/string_view.h"
#include "partition-client.h"
#include "pave-logging.h"
#include "zircon/errors.h"
#include "zircon/hw/gpt.h"

namespace paver {

namespace {

namespace block = ::llcpp::fuchsia::hardware::block;
namespace device = ::llcpp::fuchsia::device;
namespace partition = ::llcpp::fuchsia::hardware::block::partition;
namespace skipblock = ::llcpp::fuchsia::hardware::skipblock;

constexpr char kEfiName[] = "EFI Gigaboot";
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
std::unique_ptr<T> WrapUnique(T* ptr) {
  return std::unique_ptr<T>(ptr);
}

// Unbinds a device, waiting for the operation to complete. |directory| is the parent device of
// the device being removed (assumes only one child device).
zx_status_t UnbindDevice(zx::channel device, fbl::unique_fd directory, zx_duration_t timeout) {
  auto cb = [](int dirfd, int event, const char* filename, void* cookie) {
    auto device = reinterpret_cast<zx::channel*>(cookie);
    switch (event) {
      case WATCH_EVENT_REMOVE_FILE:
        return ZX_ERR_STOP;
      case WATCH_EVENT_WAITING: {
        auto resp = device::Controller::Call::ScheduleUnbind(device->borrow());
        if (resp.status() != ZX_OK) {
          return resp.status();
        }
        if (resp->result.is_err()) {
          return resp->result.err();
        }
        return ZX_OK;
      }
      default:
        return ZX_OK;
    }
  };

  zx_time_t deadline = zx_deadline_after(timeout);
  zx_status_t status = fdio_watch_directory(directory.get(), cb, deadline, &device);
  if (status == ZX_ERR_STOP) {
    status = ZX_OK;
  }
  return status;
}

// Unbinds the FVM driver from the given device. Assumes that the driver is either
// loaded or not (but not in the process of being loaded).
zx_status_t UnbindFvm(const fbl::unique_fd& devfs_root, const char* device) {
  fbl::StringBuffer<PATH_MAX> name_buffer;
  name_buffer.Append(device);
  name_buffer.Append("/fvm");

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_service_connect(name_buffer.data(), remote.release());
  if (status != ZX_OK) {
    return status;
  }

  fbl::unique_fd dir_fd(openat(devfs_root.get(), device, O_RDONLY));
  if (!dir_fd) {
    return ZX_ERR_NOT_FOUND;
  }

  return UnbindDevice(std::move(local), std::move(dir_fd), ZX_SEC(3));
}

zx_status_t OpenPartition(const fbl::unique_fd& devfs_root, const char* path,
                          fbl::Function<bool(const zx::channel&)> should_filter_file,
                          zx_duration_t timeout, zx::channel* out_partition) {
  ZX_ASSERT(path != nullptr);

  struct CallbackInfo {
    zx::channel* out_partition;
    fbl::Function<bool(const zx::channel&)> should_filter_file;
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
    fdio_cpp::UnownedFdioCaller caller(dirfd);

    zx::channel partition_local, partition_remote;
    if (zx::channel::create(0, &partition_local, &partition_remote) != ZX_OK) {
      return ZX_OK;
    }
    if (fdio_service_connect_at(caller.borrow_channel(), filename, partition_remote.release()) !=
        ZX_OK) {
      return ZX_OK;
    }
    auto info = static_cast<CallbackInfo*>(cookie);
    if (info->should_filter_file(partition_local)) {
      return ZX_OK;
    }
    if (info->out_partition) {
      *(info->out_partition) = std::move(partition_local);
    }
    return ZX_ERR_STOP;
  };

  fbl::unique_fd dir_fd(openat(devfs_root.get(), path, O_RDONLY));
  if (!dir_fd) {
    return ZX_ERR_IO;
  }

  zx_time_t deadline = zx_deadline_after(timeout);
  if (fdio_watch_directory(dir_fd.get(), cb, deadline, &info) != ZX_ERR_STOP) {
    return ZX_ERR_NOT_FOUND;
  }
  return ZX_OK;
}

constexpr char kBlockDevPath[] = "class/block/";

zx_status_t OpenBlockPartition(const fbl::unique_fd& devfs_root, const uint8_t* unique_guid,
                               const uint8_t* type_guid, zx_duration_t timeout,
                               zx::channel* out_partition) {
  ZX_ASSERT(unique_guid || type_guid);

  auto cb = [&](const zx::channel& chan) {
    if (type_guid) {
      auto result = partition::Partition::Call::GetTypeGuid(zx::unowned(chan));
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
      auto result = partition::Partition::Call::GetInstanceGuid(zx::unowned(chan));
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

  return OpenPartition(devfs_root, kBlockDevPath, cb, timeout, out_partition);
}

constexpr char kSkipBlockDevPath[] = "class/skip-block/";

zx_status_t OpenSkipBlockPartition(const fbl::unique_fd& devfs_root, const uint8_t* type_guid,
                                   zx_duration_t timeout, zx::channel* out_partition) {
  ZX_ASSERT(type_guid);

  auto cb = [&](const zx::channel& chan) {
    auto result = skipblock::SkipBlock::Call::GetPartitionInfo(zx::unowned(chan));
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

  return OpenPartition(devfs_root, kSkipBlockDevPath, cb, timeout, out_partition);
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
  zx::channel chan;
  zx_status_t status = OpenBlockPartition(devfs_root, unique_guid, type_guid, ZX_SEC(3), &chan);
  if (status != ZX_OK) {
    ERROR("Warning: Could not open partition to wipe: %s\n", zx_status_get_string(status));
    return status;
  }

  // Overwrite the first block to (hackily) ensure the destroyed partition
  // doesn't "reappear" in place.
  BlockPartitionClient block_partition(std::move(chan));
  size_t block_size;
  status = block_partition.GetBlockSize(&block_size);
  if (status != ZX_OK) {
    ERROR("Warning: Could not get block size of partition: %s\n", zx_status_get_string(status));
    return status;
  }

  // Rely on vmos being 0 initialized.
  zx::vmo vmo;
  status = zx::vmo::create(fbl::round_up(block_size, ZX_PAGE_SIZE), 0, &vmo);
  if (status != ZX_OK) {
    ERROR("Warning: Could not create vmo: %s\n", zx_status_get_string(status));
    return status;
  }

  status = block_partition.Write(vmo, block_size);
  if (status != ZX_OK) {
    ERROR("Warning: Could not write to block device: %s\n", zx_status_get_string(status));
    return status;
  }

  if ((status = block_partition.Flush()) != ZX_OK) {
    ERROR("Warning: Failed to synchronize block device: %s\n", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

zx_status_t IsBoard(const fbl::unique_fd& devfs_root, fbl::StringPiece board_name) {
  fdio_cpp::UnownedFdioCaller caller(devfs_root.get());
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }

  status = fdio_service_connect_at(caller.borrow_channel(), "sys/platform", remote.release());
  if (status != ZX_OK) {
    return status;
  }

  auto result = ::llcpp::fuchsia::sysinfo::SysInfo::Call::GetBoardName(zx::unowned(local));
  status = result.ok() ? result->status : result.status();
  if (status != ZX_OK) {
    return status;
  }
  if (result->name.size() == board_name.size() &&
      strncmp(result->name.data(), board_name.data(), result->name.size()) == 0) {
    return ZX_OK;
  }

  return ZX_ERR_NOT_SUPPORTED;
}

void utf16_to_cstring(char* dst, const uint8_t* src, size_t charcount) {
  while (charcount > 0) {
    *dst++ = *src;
    src += 2;
    charcount -= 2;
  }
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

std::unique_ptr<DevicePartitioner> DevicePartitioner::Create(fbl::unique_fd devfs_root,
                                                             zx::channel svc_root, Arch arch,
                                                             zx::channel block_device) {
  std::optional<fbl::unique_fd> block_dev;
  std::optional<fbl::unique_fd> block_dev_dup;
  std::optional<fbl::unique_fd> block_dev_dup2;
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
    block_dev_dup2 = block_dev->duplicate();
  }
  std::unique_ptr<DevicePartitioner> device_partitioner;
  if ((AstroPartitioner::Initialize(devfs_root.duplicate(), &device_partitioner) == ZX_OK) ||
      (As370Partitioner::Initialize(devfs_root.duplicate(), &device_partitioner) == ZX_OK) ||
      (SherlockPartitioner::Initialize(devfs_root.duplicate(), std::move(block_dev_dup2),
                                       &device_partitioner) == ZX_OK) ||
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

zx_status_t GptDevicePartitioner::InitializeProvidedGptDevice(
    fbl::unique_fd devfs_root, fbl::unique_fd gpt_device,
    std::unique_ptr<GptDevicePartitioner>* gpt_out) {
  fdio_cpp::UnownedFdioCaller caller(gpt_device.get());
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

  std::unique_ptr<GptDevice> gpt;
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
    auto result = ::llcpp::fuchsia::device::Controller::Call::Rebind(
        caller.channel(), fidl::StringView("/boot/driver/gpt.so"));
    if (!result.ok() || result->result.is_err()) {
      ERROR("Failed to re-read GPT\n");
      return ZX_ERR_BAD_STATE;
    }
    printf("Rebound GPT driver succesfully\n");
  }

  *gpt_out = WrapUnique(new GptDevicePartitioner(devfs_root.duplicate(), std::move(gpt_device),
                                                 std::move(gpt), *(response.info)));
  return ZX_OK;
}

zx_status_t GptDevicePartitioner::InitializeGpt(fbl::unique_fd devfs_root,
                                                std::optional<fbl::unique_fd> block_device,
                                                std::unique_ptr<GptDevicePartitioner>* gpt_out) {
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
    fdio_cpp::UnownedFdioCaller caller(gpt_device.get());
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

    std::unique_ptr<GptDevice> gpt;
    if (GptDevice::Create(gpt_device.get(), response.info->block_size, response.info->block_count,
                          &gpt) != ZX_OK) {
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
    ERROR("install-disk-image init-partition-tables --block-device %s\n", gpt_path.c_str());
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

zx_status_t GptDevicePartitioner::CreateGptPartition(const char* name, const uint8_t* type,
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
  auto result = ::llcpp::fuchsia::device::Controller::Call::Rebind(
      Channel(), fidl::StringView("/boot/driver/gpt.so"));
  if (!result.ok()) {
    ERROR("Failed to rebind GPT\n");
    return result.status();
  }
  if (result->result.is_err()) {
    ERROR("Failed to rebind GPT\n");
    return result->result.err();
  }

  return ZX_OK;
}

zx_status_t GptDevicePartitioner::AddPartition(
    const char* name, const uint8_t* type, size_t minimum_size_bytes, size_t optional_reserve_bytes,
    std::unique_ptr<PartitionClient>* out_partition) const {
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

  zx::channel chan;
  if ((status = OpenBlockPartition(devfs_root_, guid, type, ZX_SEC(15), &chan)) != ZX_OK) {
    ERROR("Added partition, waiting for bind - NOT FOUND\n");
    return status;
  }

  if (out_partition) {
    out_partition->reset(new BlockPartitionClient(std::move(chan)));
  }

  LOG("Added partition, waiting for bind - OK\n");
  return ZX_OK;
}

zx_status_t GptDevicePartitioner::FindPartition(FilterCallback filter,
                                                std::unique_ptr<PartitionClient>* out_partition,
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
      if (out_partition) {
        zx_status_t status;
        zx::channel chan;
        status = OpenBlockPartition(devfs_root_, p->guid, p->type, ZX_SEC(5), &chan);
        if (status != ZX_OK) {
          ERROR("Couldn't open partition\n");
          return status;
        }
        out_partition->reset(new BlockPartitionClient(std::move(chan)));
      }
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

zx_status_t GptDevicePartitioner::WipePartitions(FilterCallback filter) const {
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
  ::llcpp::fuchsia::device::Controller::Call::Rebind(Channel(),
                                                     fidl::StringView("/boot/driver/gpt.so"));
  return ZX_OK;
}

zx_status_t GptDevicePartitioner::WipeFvm() const { return WipePartitions(IsFvmPartition); }

zx_status_t GptDevicePartitioner::WipePartitionTables() const {
  return WipePartitions([](const gpt_partition_t&) { return true; });
}

/*====================================================*
 *                 EFI SPECIFIC                       *
 *====================================================*/

zx_status_t EfiDevicePartitioner::Initialize(fbl::unique_fd devfs_root, Arch arch,
                                             std::optional<fbl::unique_fd> block_device,
                                             std::unique_ptr<DevicePartitioner>* partitioner) {
  if (arch != Arch::kX64) {
    return ZX_ERR_NOT_FOUND;
  }

  std::unique_ptr<GptDevicePartitioner> gpt;
  zx_status_t status =
      GptDevicePartitioner::InitializeGpt(std::move(devfs_root), std::move(block_device), &gpt);
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

zx_status_t EfiDevicePartitioner::AddPartition(
    Partition partition_type, std::unique_ptr<PartitionClient>* out_partition) const {
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
      minimum_size_bytes = 32LU * (1 << 20);
      name = kZirconAName;
      break;
    }
    case Partition::kZirconB: {
      const uint8_t zircon_b_type[GPT_GUID_LEN] = GUID_ZIRCON_B_VALUE;
      memcpy(type, zircon_b_type, GPT_GUID_LEN);
      minimum_size_bytes = 32LU * (1 << 20);
      name = kZirconBName;
      break;
    }
    case Partition::kZirconR: {
      const uint8_t zircon_r_type[GPT_GUID_LEN] = GUID_ZIRCON_R_VALUE;
      memcpy(type, zircon_r_type, GPT_GUID_LEN);
      minimum_size_bytes = 48LU * (1 << 20);
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

  return gpt_->AddPartition(name, type, minimum_size_bytes, optional_reserve_bytes, out_partition);
}

zx_status_t EfiDevicePartitioner::FindPartition(
    Partition partition_type, std::unique_ptr<PartitionClient>* out_partition) const {
  switch (partition_type) {
    case Partition::kBootloader: {
      return gpt_->FindPartition(IsGigabootPartition, out_partition);
    }
    case Partition::kZirconA: {
      const auto filter = [](const gpt_partition_t& part) {
        const uint8_t guid[GPT_GUID_LEN] = GUID_ZIRCON_A_VALUE;
        return KernelFilterCallback(part, guid, kZirconAName);
      };
      return gpt_->FindPartition(filter, out_partition);
    }
    case Partition::kZirconB: {
      const auto filter = [](const gpt_partition_t& part) {
        const uint8_t guid[GPT_GUID_LEN] = GUID_ZIRCON_B_VALUE;
        return KernelFilterCallback(part, guid, kZirconBName);
      };
      return gpt_->FindPartition(filter, out_partition);
    }
    case Partition::kZirconR: {
      const auto filter = [](const gpt_partition_t& part) {
        const uint8_t guid[GPT_GUID_LEN] = GUID_ZIRCON_R_VALUE;
        return KernelFilterCallback(part, guid, kZirconRName);
      };
      return gpt_->FindPartition(filter, out_partition);
    }
    case Partition::kFuchsiaVolumeManager:
      return gpt_->FindPartition(IsFvmPartition, out_partition);

    default:
      ERROR("EFI partitioner cannot find unknown partition type\n");
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t EfiDevicePartitioner::WipeFvm() const { return gpt_->WipeFvm(); }

zx_status_t EfiDevicePartitioner::InitPartitionTables() const {
  constexpr std::array<Partition, 4> partitions = {Partition::kZirconA, Partition::kZirconB,
                                                   Partition::kZirconR,
                                                   Partition::kFuchsiaVolumeManager};

  zx_status_t status;
  for (auto partition_type : partitions) {
    std::unique_ptr<PartitionClient> partition;
    if ((status = FindPartition(partition_type, &partition)) != ZX_OK) {
      if (status != ZX_ERR_NOT_FOUND) {
        ERROR("Failure looking for partition: %s\n", zx_status_get_string(status));
        return status;
      }

      LOG("Could not find \"%s\" Partition on device. Attemping to add new partition\n",
          PartitionName(partition_type));

      if ((status = AddPartition(partition_type, &partition)) != ZX_OK) {
        ERROR("Failure creating partition: %s\n", zx_status_get_string(status));
        return status;
      }
    }
  }
  LOG("Successfully initialized gpt.\n");
  return ZX_OK;
}

zx_status_t EfiDevicePartitioner::WipePartitionTables() const {
  return gpt_->WipePartitionTables();
}
/*====================================================*
 *                CROS SPECIFIC                       *
 *====================================================*/

zx_status_t CrosDevicePartitioner::Initialize(fbl::unique_fd devfs_root, Arch arch,
                                              std::optional<fbl::unique_fd> block_device,
                                              std::unique_ptr<DevicePartitioner>* partitioner) {
  if (arch != Arch::kX64) {
    return ZX_ERR_NOT_FOUND;
  }

  std::unique_ptr<GptDevicePartitioner> gpt_partitioner;
  zx_status_t status = GptDevicePartitioner::InitializeGpt(
      std::move(devfs_root), std::move(block_device), &gpt_partitioner);
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
    llcpp::fuchsia::device::Controller::Call::Rebind(gpt_partitioner->Channel(),
                                                     fidl::StringView("/boot/driver/gpt.so"));
  }

  LOG("Successfully initialized CrOS Device Partitioner\n");
  *partitioner = WrapUnique(new CrosDevicePartitioner(std::move(gpt_partitioner)));
  return ZX_OK;
}

zx_status_t CrosDevicePartitioner::AddPartition(
    Partition partition_type, std::unique_ptr<PartitionClient>* out_partition) const {
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
    case Partition::kZirconB: {
      const uint8_t zircon_b_type[GPT_GUID_LEN] = GUID_ZIRCON_B_VALUE;
      memcpy(type, zircon_b_type, GPT_GUID_LEN);
      minimum_size_bytes = 64LU * (1 << 20);
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
      ERROR("Cros partitioner cannot add unknown partition type\n");
      return ZX_ERR_NOT_SUPPORTED;
  }
  return gpt_->AddPartition(name, type, minimum_size_bytes, optional_reserve_bytes, out_partition);
}

zx_status_t CrosDevicePartitioner::FindPartition(
    Partition partition_type, std::unique_ptr<PartitionClient>* out_partition) const {
  switch (partition_type) {
    case Partition::kZirconA: {
      const auto filter = [](const gpt_partition_t& part) {
        const uint8_t guid[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
        return KernelFilterCallback(part, guid, kZirconAName);
      };
      return gpt_->FindPartition(filter, out_partition);
    }
    case Partition::kZirconB: {
      const auto filter = [](const gpt_partition_t& part) {
        const uint8_t guid[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
        return KernelFilterCallback(part, guid, kZirconBName);
      };
      return gpt_->FindPartition(filter, out_partition);
    }
    case Partition::kZirconR: {
      const auto filter = [](const gpt_partition_t& part) {
        const uint8_t guid[GPT_GUID_LEN] = GUID_ZIRCON_R_VALUE;
        return KernelFilterCallback(part, guid, kZirconRName);
      };
      return gpt_->FindPartition(filter, out_partition);
    }
    case Partition::kFuchsiaVolumeManager:
      return gpt_->FindPartition(IsFvmPartition, out_partition);

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

zx_status_t CrosDevicePartitioner::InitPartitionTables() const {
  constexpr std::array<Partition, 4> partitions = {Partition::kZirconA, Partition::kZirconB,
                                                   Partition::kZirconR,
                                                   Partition::kFuchsiaVolumeManager};

  zx_status_t status;
  for (auto partition_type : partitions) {
    std::unique_ptr<PartitionClient> partition;
    if ((status = FindPartition(partition_type, &partition)) != ZX_OK) {
      if (status != ZX_ERR_NOT_FOUND) {
        ERROR("Failure looking for partition: %s\n", zx_status_get_string(status));
        return status;
      }

      LOG("Could not find \"%s\" Partition on device. Attemping to add new partition\n",
          PartitionName(partition_type));

      if ((status = AddPartition(partition_type, &partition)) != ZX_OK) {
        ERROR("Failure creating partition: %s\n", zx_status_get_string(status));
        return status;
      }
    }
  }
  LOG("Successfully initialized gpt.\n");
  return ZX_OK;
}

zx_status_t CrosDevicePartitioner::WipePartitionTables() const {
  return gpt_->WipePartitionTables();
}
/*====================================================*
 *               FIXED PARTITION MAP                  *
 *====================================================*/

zx_status_t FixedDevicePartitioner::Initialize(fbl::unique_fd devfs_root,
                                               std::unique_ptr<DevicePartitioner>* partitioner) {
  if (HasSkipBlockDevice(devfs_root)) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  LOG("Successfully initialized FixedDevicePartitioner Device Partitioner\n");
  *partitioner = WrapUnique(new FixedDevicePartitioner(std::move(devfs_root)));
  return ZX_OK;
}

zx_status_t FixedDevicePartitioner::AddPartition(
    Partition partition_type, std::unique_ptr<PartitionClient>* out_partition) const {
  ERROR("Cannot add partitions to a fixed-map partition device\n");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FixedDevicePartitioner::FindPartition(
    Partition partition_type, std::unique_ptr<PartitionClient>* out_partition) const {
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
    case Partition::kABRMeta: {
      const uint8_t abr_meta_type[GPT_GUID_LEN] = GUID_ABR_META_VALUE;
      memcpy(type, abr_meta_type, GPT_GUID_LEN);
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

  zx::channel chan;
  zx_status_t status = OpenBlockPartition(devfs_root_, nullptr, type, ZX_SEC(5), &chan);
  if (status != ZX_OK) {
    return status;
  }

  out_partition->reset(new BlockPartitionClient(std::move(chan)));
  return ZX_OK;
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

zx_status_t FixedDevicePartitioner::InitPartitionTables() const { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t FixedDevicePartitioner::WipePartitionTables() const { return ZX_ERR_NOT_SUPPORTED; }

/*====================================================*
 *                    SHERLOCK                        *
 *====================================================*/

zx_status_t SherlockPartitioner::Initialize(fbl::unique_fd devfs_root,
                                            std::optional<fbl::unique_fd> block_device,
                                            std::unique_ptr<DevicePartitioner>* partitioner) {
  if (IsBoard(devfs_root, "sherlock") != ZX_OK) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  std::unique_ptr<GptDevicePartitioner> gpt;
  zx_status_t status =
      GptDevicePartitioner::InitializeGpt(std::move(devfs_root), std::move(block_device), &gpt);
  if (status != ZX_OK) {
    return status;
  }

  LOG("Successfully initialized SherlockPartitioner Device Partitioner\n");
  *partitioner = WrapUnique(new SherlockPartitioner(std::move(gpt)));
  return ZX_OK;
}

zx_status_t SherlockPartitioner::AddPartition(
    Partition partition_type, std::unique_ptr<PartitionClient>* out_partition) const {
  ERROR("Cannot add partitions to a sherlock device\n");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SherlockPartitioner::FindPartition(
    Partition partition_type, std::unique_ptr<PartitionClient>* out_partition) const {
  uint8_t type[GPT_GUID_LEN];

  switch (partition_type) {
    case Partition::kBootloader: {
      const uint8_t boot0_type[GPT_GUID_LEN] = GUID_EMMC_BOOT1_VALUE;
      zx::channel chan;
      zx_status_t status =
          OpenBlockPartition(gpt_->devfs_root(), nullptr, boot0_type, ZX_SEC(5), &chan);
      if (status != ZX_OK) {
        return status;
      }
      auto boot0 = std::make_unique<BlockPartitionClient>(std::move(chan));

      const uint8_t boot1_type[GPT_GUID_LEN] = GUID_EMMC_BOOT2_VALUE;
      status = OpenBlockPartition(gpt_->devfs_root(), nullptr, boot1_type, ZX_SEC(5), &chan);
      if (status != ZX_OK) {
        return status;
      }
      auto boot1 = std::make_unique<BlockPartitionClient>(std::move(chan));

      std::vector<std::unique_ptr<PartitionClient>> partitions;
      partitions.push_back(std::move(boot0));
      partitions.push_back(std::move(boot1));
      *out_partition = std::make_unique<PartitionCopyClient>(std::move(partitions));

      return ZX_OK;
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
    case Partition::kABRMeta: {
      const uint8_t abr_meta_type[GPT_GUID_LEN] = GUID_ABR_META_VALUE;
      memcpy(type, abr_meta_type, GPT_GUID_LEN);
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

  const auto filter = [type](const gpt_partition_t& part) {
    return memcmp(part.type, type, GPT_GUID_LEN) == 0;
  };
  return gpt_->FindPartition(std::move(filter), out_partition);
}

zx_status_t SherlockPartitioner::WipeFvm() const { return gpt_->WipeFvm(); }

zx_status_t SherlockPartitioner::InitPartitionTables() const {
  struct Partition {
    const char* name;
    uint8_t type[GPT_GUID_LEN];
    size_t min_size;
  };

  const auto add_partitions = [&](fbl::Span<const Partition> partitions) {
    for (const auto& part : partitions) {
      std::unique_ptr<PartitionClient> out;
      if (auto status = gpt_->AddPartition(part.name, part.type, part.min_size, 0, &out);
          status != ZX_OK) {
        return status;
      }
    }
    return ZX_OK;
  };

  const char* partitions_to_wipe[] = {
      "recovery",
      "boot",
      "system",
      kFvmPartitionName,
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

  if (auto status = gpt_->WipePartitions(wipe); status != ZX_OK) {
    return status;
  }

  constexpr size_t kKibibyte = 1024;
  constexpr size_t kMebibyte = kKibibyte * 1024;

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

  if (auto status = add_partitions(fbl::Span(partitions_to_add)); status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t SherlockPartitioner::WipePartitionTables() const { return ZX_ERR_NOT_SUPPORTED; }

/*====================================================*
 *                SKIP BLOCK SPECIFIC                 *
 *====================================================*/

zx_status_t SkipBlockDevicePartitioner::FindPartition(
    const uint8_t* type, std::unique_ptr<PartitionClient>* out_partition) const {
  zx::channel chan;
  zx_status_t status = OpenSkipBlockPartition(devfs_root_, type, ZX_SEC(5), &chan);
  if (status != ZX_OK) {
    return status;
  }

  out_partition->reset(new SkipBlockPartitionClient(std::move(chan)));
  return ZX_OK;
}

zx_status_t SkipBlockDevicePartitioner::FindFvmPartition(
    std::unique_ptr<PartitionClient>* out_partition) const {
  const uint8_t fvm_type[GPT_GUID_LEN] = GUID_FVM_VALUE;
  // FVM partition is managed so it should expose a normal block device.
  zx::channel chan;
  zx_status_t status = OpenBlockPartition(devfs_root_, nullptr, fvm_type, ZX_SEC(5), &chan);
  if (status != ZX_OK) {
    return status;
  }

  out_partition->reset(new BlockPartitionClient(std::move(chan)));
  return ZX_OK;
}

zx_status_t SkipBlockDevicePartitioner::WipeFvm() const {
  const uint8_t fvm_type[GPT_GUID_LEN] = GUID_FVM_VALUE;
  zx::channel chan;
  zx_status_t status = OpenBlockPartition(devfs_root_, nullptr, fvm_type, ZX_SEC(3), &chan);
  if (status != ZX_OK) {
    ERROR("Warning: Could not open partition to wipe: %s\n", zx_status_get_string(status));
    return ZX_OK;
  }

  device::Controller::SyncClient block_client(std::move(chan));

  auto result = block_client.GetTopologicalPath();
  if (!result.ok()) {
    ERROR("Warning: Could not get name for partition: %s\n", zx_status_get_string(result.status()));
    return result.status();
  }
  const auto& response = result.value();
  if (response.result.is_err()) {
    ERROR("Warning: Could not get name for partition: %s\n",
          zx_status_get_string(response.result.err()));
    return response.result.err();
  }

  fbl::StringBuffer<PATH_MAX> name_buffer;
  name_buffer.Append(response.result.response().path.data(),
                     static_cast<size_t>(response.result.response().path.size()));

  status = UnbindFvm(devfs_root_, name_buffer.data());
  if (status != ZX_OK) {
    // The driver may refuse to bind to a corrupt volume.
    ERROR("Warning: Failed to unbind FVM: %s\n", zx_status_get_string(status));
  }

  // TODO(39761): Clean this up.
  const char* parent = dirname(name_buffer.data());
  constexpr char kDevRoot[] = "/dev/";
  constexpr size_t kDevRootLen = sizeof(kDevRoot) - 1;
  if (strncmp(parent, kDevRoot, kDevRootLen) != 0) {
    ERROR("Warning: Unrecognized partition name: %s\n", parent);
    return ZX_ERR_NOT_SUPPORTED;
  }
  parent += kDevRootLen;

  zx::channel local, remote;
  status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    ERROR("Warning: Failed to create channel pair: %s\n", zx_status_get_string(status));
    return status;
  }
  fdio_cpp::UnownedFdioCaller caller(devfs_root_.get());
  status = fdio_service_connect_at(caller.borrow_channel(), parent, remote.release());
  if (status != ZX_OK) {
    ERROR("Warning: Unable to open block parent device: %s\n", zx_status_get_string(status));
    return status;
  }

  block::Ftl::SyncClient client(std::move(local));
  auto result2 = client.Format();

  return result2.ok() ? result2.value().status : result2.status();
}

zx_status_t AstroPartitioner::Initialize(fbl::unique_fd devfs_root,
                                         std::unique_ptr<DevicePartitioner>* partitioner) {
  if (IsBoard(devfs_root, "astro") != ZX_OK) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  LOG("Successfully initialized AstroPartitioner Device Partitioner\n");
  std::unique_ptr<SkipBlockDevicePartitioner> skip_block(
      new SkipBlockDevicePartitioner(std::move(devfs_root)));

  *partitioner = WrapUnique(new AstroPartitioner(std::move(skip_block)));
  return ZX_OK;
}

zx_status_t AstroPartitioner::AddPartition(Partition partition_type,
                                           std::unique_ptr<PartitionClient>* out_partition) const {
  ERROR("Cannot add partitions to an astro.\n");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AstroPartitioner::FindPartition(Partition partition_type,
                                            std::unique_ptr<PartitionClient>* out_partition) const {
  switch (partition_type) {
    case Partition::kBootloader: {
      const uint8_t bl2_type[GPT_GUID_LEN] = GUID_BL2_VALUE;
      std::unique_ptr<PartitionClient> bl2_skip_block;
      if (auto status = skip_block_->FindPartition(bl2_type, &bl2_skip_block); status != ZX_OK) {
        return status;
      }
      // Upgrade this into a more specialized partition client.
      auto bl2 = std::make_unique<Bl2PartitionClient>(bl2_skip_block->GetChannel());

      const uint8_t tpl_type[GPT_GUID_LEN] = GUID_BOOTLOADER_VALUE;
      std::unique_ptr<PartitionClient> tpl;
      if (auto status = skip_block_->FindPartition(tpl_type, &tpl); status != ZX_OK) {
        return status;
      }

      *out_partition =
          std::make_unique<AstroBootloaderPartitionClient>(std::move(bl2), std::move(tpl));
      return ZX_OK;
    }
    case Partition::kZirconA: {
      const uint8_t zircon_a_type[GPT_GUID_LEN] = GUID_ZIRCON_A_VALUE;
      return skip_block_->FindPartition(zircon_a_type, out_partition);
    }
    case Partition::kZirconB: {
      const uint8_t zircon_b_type[GPT_GUID_LEN] = GUID_ZIRCON_B_VALUE;
      return skip_block_->FindPartition(zircon_b_type, out_partition);
    }
    case Partition::kZirconR: {
      const uint8_t zircon_r_type[GPT_GUID_LEN] = GUID_ZIRCON_R_VALUE;
      return skip_block_->FindPartition(zircon_r_type, out_partition);
    }
    case Partition::kVbMetaA:
    case Partition::kVbMetaB:
    case Partition::kVbMetaR:
    case Partition::kABRMeta: {
      const auto type = [&]() {
        switch (partition_type) {
          case Partition::kVbMetaA:
            return sysconfig::SyncClient::PartitionType::kVerifiedBootMetadataA;
          case Partition::kVbMetaB:
            return sysconfig::SyncClient::PartitionType::kVerifiedBootMetadataB;
          case Partition::kVbMetaR:
            return sysconfig::SyncClient::PartitionType::kVerifiedBootMetadataR;
          case Partition::kABRMeta:
            return sysconfig::SyncClient::PartitionType::kABRMetadata;
          default:
            break;
        }
        ZX_ASSERT(false);
      }();
      std::optional<sysconfig::SyncClient> client;
      zx_status_t status = sysconfig::SyncClient::Create(skip_block_->devfs_root(), &client);
      if (status != ZX_OK) {
        return status;
      }
      out_partition->reset(new SysconfigPartitionClient(*std::move(client), type));
      return ZX_OK;
    }
    case Partition::kFuchsiaVolumeManager: {
      return skip_block_->FindFvmPartition(out_partition);
    }
    default:
      ERROR("partition_type is invalid!\n");
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t AstroPartitioner::WipeFvm() const { return skip_block_->WipeFvm(); }

zx_status_t AstroPartitioner::InitPartitionTables() const { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t AstroPartitioner::WipePartitionTables() const { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t As370Partitioner::Initialize(fbl::unique_fd devfs_root,
                                         std::unique_ptr<DevicePartitioner>* partitioner) {
  if (IsBoard(devfs_root, "visalia") != ZX_OK) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  LOG("Successfully initialized As370Partitioner Device Partitioner\n");

  std::unique_ptr<SkipBlockDevicePartitioner> skip_block(
      new SkipBlockDevicePartitioner(std::move(devfs_root)));
  *partitioner = WrapUnique(new As370Partitioner(std::move(skip_block)));
  return ZX_OK;
}

zx_status_t As370Partitioner::AddPartition(Partition partition_type,
                                           std::unique_ptr<PartitionClient>* out_partition) const {
  ERROR("Cannot add partitions to an as370.\n");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370Partitioner::FindPartition(Partition partition_type,
                                            std::unique_ptr<PartitionClient>* out_partition) const {
  switch (partition_type) {
    case Partition::kBootloader: {
      const uint8_t bootloader_type[GPT_GUID_LEN] = GUID_BOOTLOADER_VALUE;
      return skip_block_->FindPartition(bootloader_type, out_partition);
    }
    case Partition::kZirconA: {
      const uint8_t zircon_a_type[GPT_GUID_LEN] = GUID_ZIRCON_A_VALUE;
      return skip_block_->FindPartition(zircon_a_type, out_partition);
    }
    case Partition::kZirconB: {
      const uint8_t zircon_b_type[GPT_GUID_LEN] = GUID_ZIRCON_B_VALUE;
      return skip_block_->FindPartition(zircon_b_type, out_partition);
    }
    case Partition::kZirconR: {
      const uint8_t zircon_r_type[GPT_GUID_LEN] = GUID_ZIRCON_R_VALUE;
      return skip_block_->FindPartition(zircon_r_type, out_partition);
    }
    case Partition::kFuchsiaVolumeManager: {
      return skip_block_->FindFvmPartition(out_partition);
    }
    default:
      ERROR("partition_type is invalid!\n");
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t As370Partitioner::WipeFvm() const { return skip_block_->WipeFvm(); }

zx_status_t As370Partitioner::InitPartitionTables() const { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t As370Partitioner::WipePartitionTables() const { return ZX_ERR_NOT_SUPPORTED; }

}  // namespace paver
