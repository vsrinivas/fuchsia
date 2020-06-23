// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device-partitioner.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/boot/llcpp/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/fshost/llcpp/fidl.h>
#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <fuchsia/hardware/block/partition/llcpp/fidl.h>
#include <fuchsia/hardware/skipblock/llcpp/fidl.h>
#include <fuchsia/sysinfo/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/status.h>
#include <libgen.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <array>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <chromeos-disk-setup/chromeos-disk-setup.h>
#include <fbl/auto_call.h>
#include <fbl/function.h>
#include <fbl/span.h>
#include <fbl/string_buffer.h>
#include <fbl/string_printf.h>
#include <fs-management/fvm.h>
#include <gpt/cros.h>
#include <soc/aml-common/aml-guid.h>
#include <zxcrypt/volume.h>

#include "abr-client.h"
#include "fvm.h"
#include "partition-client.h"
#include "pave-logging.h"
#include "validation.h"

namespace paver {

// Not static so test can manipulate it.
zx_duration_t g_wipe_timeout = ZX_SEC(3);

namespace {

namespace block = ::llcpp::fuchsia::hardware::block;
namespace device = ::llcpp::fuchsia::device;
namespace partition = ::llcpp::fuchsia::hardware::block::partition;
namespace skipblock = ::llcpp::fuchsia::hardware::skipblock;

constexpr size_t kKibibyte = 1024;
constexpr size_t kMebibyte = kKibibyte * 1024;
constexpr size_t kGibibyte = kMebibyte * 1024;

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

bool FilterByType(const gpt_partition_t& part, const std::array<uint8_t, GPT_GUID_LEN>& type) {
  return memcmp(part.type, type.data(), GPT_GUID_LEN) == 0;
}

bool FilterByTypeAndName(const gpt_partition_t& part, const std::array<uint8_t, GPT_GUID_LEN>& type,
                         fbl::StringPiece name) {
  char cstring_name[GPT_NAME_LEN];
  utf16_to_cstring(cstring_name, reinterpret_cast<const uint16_t*>(part.name), GPT_NAME_LEN);
  return memcmp(part.type, type.data(), GPT_GUID_LEN) == 0 &&
         // We use a case-insenstive comparison to be compatible with the previous naming scheme.
         // On a ChromeOS device, all of the kernel partitions share a common GUID type, so we
         // distinguish Zircon kernel partitions based on name.
         strncasecmp(cstring_name, name.data(), name.length()) == 0;
}

bool IsFvmPartition(const gpt_partition_t& part) {
  const std::array<uint8_t, GPT_GUID_LEN> partition_type = GUID_FVM_VALUE;
  return FilterByType(part, partition_type);
}

// Returns true if the spec partition is Zircon A/B/R.
bool IsZirconPartitionSpec(const PartitionSpec& spec) {
  return spec.partition == Partition::kZirconA || spec.partition == Partition::kZirconB ||
         spec.partition == Partition::kZirconR;
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

zx::status<zx::channel> OpenPartition(const fbl::unique_fd& devfs_root, const char* path,
                                      fbl::Function<bool(const zx::channel&)> should_filter_file,
                                      zx_duration_t timeout) {
  ZX_ASSERT(path != nullptr);

  struct CallbackInfo {
    zx::channel out_partition;
    fbl::Function<bool(const zx::channel&)> should_filter_file;
  };

  CallbackInfo info = {
      .out_partition = zx::channel(),
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
    info->out_partition = std::move(partition_local);
    return ZX_ERR_STOP;
  };

  fbl::unique_fd dir_fd(openat(devfs_root.get(), path, O_RDONLY));
  if (!dir_fd) {
    return zx::error(ZX_ERR_IO);
  }

  zx_time_t deadline = zx_deadline_after(timeout);
  if (fdio_watch_directory(dir_fd.get(), cb, deadline, &info) != ZX_ERR_STOP) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  return zx::ok(std::move(info.out_partition));
}

constexpr char kBlockDevPath[] = "class/block/";

zx::status<zx::channel> OpenBlockPartition(const fbl::unique_fd& devfs_root,
                                           const uint8_t* unique_guid, const uint8_t* type_guid,
                                           zx_duration_t timeout) {
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

  return OpenPartition(devfs_root, kBlockDevPath, cb, timeout);
}

constexpr char kSkipBlockDevPath[] = "class/skip-block/";

zx::status<zx::channel> OpenSkipBlockPartition(const fbl::unique_fd& devfs_root,
                                               const uint8_t* type_guid, zx_duration_t timeout) {
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

  return OpenPartition(devfs_root, kSkipBlockDevPath, cb, timeout);
}

bool HasSkipBlockDevice(const fbl::unique_fd& devfs_root) {
  // Our proxy for detected a skip-block device is by checking for the
  // existence of a device enumerated under the skip-block class.
  const uint8_t type[GPT_GUID_LEN] = GUID_ZIRCON_A_VALUE;
  return OpenSkipBlockPartition(devfs_root, type, ZX_SEC(1)).is_ok();
}

// Attempts to open and overwrite the first block of the underlying
// partition. Does not rebind partition drivers.
//
// At most one of |unique_guid| and |type_guid| may be nullptr.
zx::status<> WipeBlockPartition(const fbl::unique_fd& devfs_root, const uint8_t* unique_guid,
                                const uint8_t* type_guid) {
  auto status = OpenBlockPartition(devfs_root, unique_guid, type_guid, g_wipe_timeout);
  if (status.is_error()) {
    ERROR("Warning: Could not open partition to wipe: %s\n", status.status_string());
    return status.take_error();
  }

  // Overwrite the first block to (hackily) ensure the destroyed partition
  // doesn't "reappear" in place.
  BlockPartitionClient block_partition(std::move(status.value()));
  auto status2 = block_partition.GetBlockSize();
  if (status2.is_error()) {
    ERROR("Warning: Could not get block size of partition: %s\n", status2.status_string());
    return status2.take_error();
  }
  const size_t block_size = status2.value();

  // Rely on vmos being 0 initialized.
  zx::vmo vmo;
  {
    auto status =
        zx::make_status(zx::vmo::create(fbl::round_up(block_size, ZX_PAGE_SIZE), 0, &vmo));
    if (status.is_error()) {
      ERROR("Warning: Could not create vmo: %s\n", status.status_string());
      return status.take_error();
    }
  }

  if (auto status = block_partition.Write(vmo, block_size); status.is_error()) {
    ERROR("Warning: Could not write to block device: %s\n", status.status_string());
    return status.take_error();
  }

  if (auto status = block_partition.Flush(); status.is_error()) {
    ERROR("Warning: Failed to synchronize block device: %s\n", status.status_string());
    return status.take_error();
  }

  return zx::ok();
}

zx::status<> IsBoard(const fbl::unique_fd& devfs_root, fbl::StringPiece board_name) {
  zx::channel local, remote;
  auto status = zx::make_status(zx::channel::create(0, &local, &remote));
  if (status.is_error()) {
    return status.take_error();
  }

  fdio_cpp::UnownedFdioCaller caller(devfs_root.get());
  status = zx::make_status(
      fdio_service_connect_at(caller.borrow_channel(), "sys/platform", remote.release()));
  if (status.is_error()) {
    return status.take_error();
  }

  auto result = ::llcpp::fuchsia::sysinfo::SysInfo::Call::GetBoardName(zx::unowned(local));
  status = zx::make_status(result.ok() ? result->status : result.status());
  if (status.is_error()) {
    return status.take_error();
  }
  if (strncmp(result->name.data(), board_name.data(), result->name.size()) == 0) {
    return zx::ok();
  }

  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> IsBootloader(const fbl::unique_fd& devfs_root, fbl::StringPiece vendor) {
  zx::channel local, remote;
  zx::status<> status = zx::make_status(zx::channel::create(0, &local, &remote));
  if (status.is_error()) {
    return status.take_error();
  }

  fdio_cpp::UnownedFdioCaller caller(devfs_root.get());
  status = zx::make_status(
      fdio_service_connect_at(caller.borrow_channel(), "sys/platform", remote.release()));
  if (status.is_error()) {
    return status.take_error();
  }

  auto result = ::llcpp::fuchsia::sysinfo::SysInfo::Call::GetBootloaderVendor(zx::unowned(local));
  status = zx::make_status(result.ok() ? result->status : result.status());
  if (status.is_error()) {
    return status.take_error();
  }
  if (strncmp(result->vendor.data(), vendor.data(), result->vendor.size()) == 0) {
    return zx::ok();
  }

  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

void utf16_to_cstring(char* dst, const uint8_t* src, size_t charcount) {
  while (charcount > 0) {
    *dst++ = *src;
    src += 2;
    charcount -= 2;
  }
}

using GptGuid = std::array<uint8_t, GPT_GUID_LEN>;

zx::status<GptGuid> GptPartitionType(Partition type) {
  switch (type) {
    case Partition::kBootloader:
      return zx::ok(GptGuid(GUID_EFI_VALUE));
    case Partition::kZirconA:
      return zx::ok(GptGuid(GUID_ZIRCON_A_VALUE));
    case Partition::kZirconB:
      return zx::ok(GptGuid(GUID_ZIRCON_B_VALUE));
    case Partition::kZirconR:
      return zx::ok(GptGuid(GUID_ZIRCON_R_VALUE));
    case Partition::kVbMetaA:
      return zx::ok(GptGuid(GUID_VBMETA_A_VALUE));
    case Partition::kVbMetaB:
      return zx::ok(GptGuid(GUID_VBMETA_B_VALUE));
    case Partition::kVbMetaR:
      return zx::ok(GptGuid(GUID_VBMETA_R_VALUE));
    case Partition::kAbrMeta:
      return zx::ok(GptGuid(GUID_ABR_META_VALUE));
    case Partition::kFuchsiaVolumeManager:
      return zx::ok(GptGuid(GUID_FVM_VALUE));
    default:
      ERROR("Partition type is invalid\n");
      return zx::error(ZX_ERR_INVALID_ARGS);
  }
}

zx::status<GptGuid> CrosPartitionType(Partition type) {
  switch (type) {
    case Partition::kZirconA:
    case Partition::kZirconB:
    case Partition::kZirconR:
      return zx::ok(GptGuid(GUID_CROS_KERNEL_VALUE));
    default:
      return GptPartitionType(type);
  }
}

bool SpecMatches(const PartitionSpec& a, const PartitionSpec& b) {
  return a.partition == b.partition && a.content_type == b.content_type;
}

std::optional<::llcpp::fuchsia::boot::Arguments::SyncClient> OpenBootArgumentClient(
    const zx::channel& svc_root) {
  if (!svc_root.is_valid()) {
    return {};
  }

  zx::channel local, remote;
  if (zx_status_t status = zx::channel::create(0, &local, &remote); status != ZX_OK) {
    ERROR("Failed to create channel. \n");
    return {};
  }

  auto status = fdio_service_connect_at(svc_root.get(), ::llcpp::fuchsia::boot::Arguments::Name,
                                        remote.release());
  if (status != ZX_OK) {
    ERROR("Failed to connect to boot::Arguments service.\n");
    return {};
  }

  return {::llcpp::fuchsia::boot::Arguments::SyncClient(std::move(local))};
}

bool GetBool(::llcpp::fuchsia::boot::Arguments::SyncClient& client, ::fidl::StringView key,
             bool default_on_missing_or_failure) {
  auto key_data = key.data();
  auto result = client.GetBool(std::move(key), default_on_missing_or_failure);
  if (!result.ok()) {
    ERROR("Failed to get boolean argument %s. Default to %d.\n", key_data,
          default_on_missing_or_failure);
    return default_on_missing_or_failure;
  }
  return result->value;
}

}  // namespace

BlockWatcherPauser::~BlockWatcherPauser() {
  if (valid_) {
    auto result = watcher_.Resume();
    if (result.status() != ZX_OK) {
      ERROR("Failed to unpause the block watcher: %s\n", zx_status_get_string(result.status()));
    } else if (result->status != ZX_OK) {
      ERROR("Failed to unpause the block watcher: %s\n", zx_status_get_string(result->status));
    }
  }
}

zx::status<BlockWatcherPauser> BlockWatcherPauser::Create(const zx::channel& svc_root) {
  zx::channel local, remote;
  auto status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  status = fdio_service_connect_at(svc_root.get(), llcpp::fuchsia::fshost::BlockWatcher::Name,
                                   remote.release());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  BlockWatcherPauser pauser(std::move(local));
  if (auto status = pauser.Pause(); status.is_error()) {
    return status.take_error();
  }

  return zx::ok(std::move(pauser));
}

zx::status<> BlockWatcherPauser::Pause() {
  auto result = watcher_.Pause();
  auto status = zx::make_status(result.ok() ? result->status : result.status());

  valid_ = status.is_ok();
  return status;
}

const char* PartitionName(Partition type) {
  switch (type) {
    case Partition::kBootloader:
      return GUID_EFI_NAME;
    case Partition::kZirconA:
      return GUID_ZIRCON_A_NAME;
    case Partition::kZirconB:
      return GUID_ZIRCON_B_NAME;
    case Partition::kZirconR:
      return GUID_ZIRCON_R_NAME;
    case Partition::kVbMetaA:
      return GUID_VBMETA_A_NAME;
    case Partition::kVbMetaB:
      return GUID_VBMETA_B_NAME;
    case Partition::kVbMetaR:
      return GUID_VBMETA_R_NAME;
    case Partition::kAbrMeta:
      return GUID_ABR_META_NAME;
    case Partition::kFuchsiaVolumeManager:
      return GUID_FVM_NAME;
    default:
      return "Unknown";
  }
}

fbl::String PartitionSpec::ToString() const {
  if (content_type.empty()) {
    return PartitionName(partition);
  }
  return fbl::StringPrintf("%s (%.*s)", PartitionName(partition),
                           static_cast<int>(content_type.size()), content_type.data());
}

std::unique_ptr<DevicePartitioner> DevicePartitioner::Create(fbl::unique_fd devfs_root,
                                                             const zx::channel& svc_root, Arch arch,
                                                             std::shared_ptr<Context> context,
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
  if (auto status = AstroPartitioner::Initialize(devfs_root.duplicate(), svc_root, context);
      status.is_ok()) {
    return std::move(status.value());
  }
  if (auto status = As370Partitioner::Initialize(devfs_root.duplicate()); status.is_ok()) {
    return std::move(status.value());
  }
  if (auto status = SherlockPartitioner::Initialize(devfs_root.duplicate(), svc_root,
                                                    std::move(block_dev_dup2));
      status.is_ok()) {
    return std::move(status.value());
  }
  if (auto status = CrosDevicePartitioner::Initialize(devfs_root.duplicate(), svc_root, arch,
                                                      std::move(block_dev_dup));
      status.is_ok()) {
    return std::move(status.value());
  }
  if (auto status = EfiDevicePartitioner::Initialize(devfs_root.duplicate(), svc_root, arch,
                                                     std::move(block_dev));
      status.is_ok()) {
    return std::move(status.value());
  }
  if (auto status = FixedDevicePartitioner::Initialize(std::move(devfs_root)); status.is_ok()) {
    return std::move(status.value());
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
    fbl::unique_fd devfs_root, const zx::channel& svc_root,
    std::optional<fbl::unique_fd> block_device) {
  if (block_device) {
    auto status =
        InitializeProvidedGptDevice(std::move(devfs_root), svc_root, *std::move(block_device));
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

zx::status<std::array<uint8_t, GPT_GUID_LEN>> GptDevicePartitioner::CreateGptPartition(
    const char* name, const uint8_t* type, uint64_t offset, uint64_t blocks) const {
  std::array<uint8_t, GPT_GUID_LEN> guid;
  zx_cprng_draw(guid.data(), GPT_GUID_LEN);

  zx_status_t status;
  if ((status = gpt_->AddPartition(name, type, guid.data(), offset, blocks, 0)) != ZX_OK) {
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
    const char* name, const uint8_t* type, size_t minimum_size_bytes,
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

  auto status_or_part =
      OpenBlockPartition(devfs_root_, status_or_guid.value().data(), type, ZX_SEC(15));
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
      auto status = OpenBlockPartition(devfs_root_, p->guid, p->type, ZX_SEC(5));
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
    WipeBlockPartition(devfs_root_, p->guid, p->type).status_value();

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

/*====================================================*
 *                 EFI SPECIFIC                       *
 *====================================================*/

zx::status<std::unique_ptr<DevicePartitioner>> EfiDevicePartitioner::Initialize(
    fbl::unique_fd devfs_root, const zx::channel& svc_root, Arch arch,
    std::optional<fbl::unique_fd> block_device) {
  if (arch != Arch::kX64) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  auto status =
      GptDevicePartitioner::InitializeGpt(std::move(devfs_root), svc_root, std::move(block_device));
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
  const PartitionSpec supported_specs[] = {PartitionSpec(paver::Partition::kBootloader),
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
    case Partition::kBootloader:
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

  const char* name = PartitionName(spec.partition);
  auto type = GptPartitionType(spec.partition);
  if (type.is_error()) {
    return type.take_error();
  }
  return gpt_->AddPartition(name, type->data(), minimum_size_bytes, /*optional_reserve_bytes*/ 0);
}

zx::status<std::unique_ptr<PartitionClient>> EfiDevicePartitioner::FindPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  switch (spec.partition) {
    case Partition::kBootloader: {
      const auto filter = [](const gpt_partition_t& part) {
        const std::array<uint8_t, GPT_GUID_LEN> partition_type = GUID_EFI_VALUE;
        return FilterByTypeAndName(part, partition_type, GUID_EFI_NAME);
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
      Partition::kBootloader, Partition::kZirconA, Partition::kZirconB,
      Partition::kZirconR,    Partition::kVbMetaA, Partition::kVbMetaB,
      Partition::kVbMetaR,    Partition::kAbrMeta, Partition::kFuchsiaVolumeManager,
  };

  // Wipe partitions.
  // EfiDevicePartitioner operates on partition types.
  auto status = gpt_->WipePartitions([&partitions_to_add](const gpt_partition_t& part) {
    for (auto& partition : partitions_to_add) {
      // Get the partition type GUID, and compare it.
      auto status = GptPartitionType(partition);
      if (status.is_error() || memcmp(part.type, status->data(), GPT_GUID_LEN) != 0) {
        continue;
      }
      // If we are wiping any non-bootloader partition, we are done.
      if (partition != Partition::kBootloader) {
        return true;
      }
      // If we are wiping the bootloader partition, only do so if it is the
      // Fuchsia-installed bootloader partition. This is to allow dual-booting.
      char cstring_name[GPT_NAME_LEN] = {};
      utf16_to_cstring(cstring_name, part.name, GPT_NAME_LEN);
      if (strncasecmp(cstring_name, GUID_EFI_NAME, GPT_NAME_LEN) == 0) {
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
      ERROR("Warning: Skipping existing partition \"%s\"\n", PartitionName(type));
    } else if (status.is_error()) {
      ERROR("Failed to create partition \"%s\": %s\n", PartitionName(type), status.status_string());
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

/*====================================================*
 *                CROS SPECIFIC                       *
 *====================================================*/

zx::status<std::unique_ptr<DevicePartitioner>> CrosDevicePartitioner::Initialize(
    fbl::unique_fd devfs_root, const zx::channel& svc_root, Arch arch,
    std::optional<fbl::unique_fd> block_device) {
  if (arch != Arch::kX64) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  zx::status<> status = IsBootloader(devfs_root, "coreboot");
  if (status.is_error()) {
    return status.take_error();
  }

  auto status_or_gpt =
      GptDevicePartitioner::InitializeGpt(std::move(devfs_root), svc_root, std::move(block_device));
  if (status_or_gpt.is_error()) {
    return status_or_gpt.take_error();
  }
  std::unique_ptr<GptDevicePartitioner>& gpt_partitioner = status_or_gpt->gpt;

  GptDevice* gpt = gpt_partitioner->GetGpt();
  block::BlockInfo info = gpt_partitioner->GetBlockInfo();

  if (!is_ready_to_pave(gpt, reinterpret_cast<fuchsia_hardware_block_BlockInfo*>(&info),
                        SZ_ZX_PART)) {
    auto pauser = BlockWatcherPauser::Create(gpt_partitioner->svc_root());
    if (pauser.is_error()) {
      ERROR("Failed to pause the block watcher");
      return pauser.take_error();
    }

    auto status = zx::make_status(config_cros_for_fuchsia(
        gpt, reinterpret_cast<fuchsia_hardware_block_BlockInfo*>(&info), SZ_ZX_PART));
    if (status.is_error()) {
      ERROR("Failed to configure CrOS for Fuchsia.\n");
      return status.take_error();
    }
    if (auto status = zx::make_status(gpt->Sync()); status.is_error()) {
      ERROR("Failed to sync CrOS for Fuchsia.\n");
      return status.take_error();
    }
    __UNUSED auto unused =
        RebindGptDriver(gpt_partitioner->svc_root(), gpt_partitioner->Channel()).status_value();
  }

  auto partitioner = WrapUnique(new CrosDevicePartitioner(std::move(gpt_partitioner)));
  if (status_or_gpt->initialize_partition_tables) {
    if (auto status = partitioner->InitPartitionTables(); status.is_error()) {
      return status.take_error();
    }
  }

  LOG("Successfully initialized CrOS Device Partitioner\n");
  return zx::ok(std::move(partitioner));
}

bool CrosDevicePartitioner::SupportsPartition(const PartitionSpec& spec) const {
  const PartitionSpec supported_specs[] = {PartitionSpec(paver::Partition::kZirconA),
                                           PartitionSpec(paver::Partition::kZirconB),
                                           PartitionSpec(paver::Partition::kZirconR),
                                           PartitionSpec(paver::Partition::kFuchsiaVolumeManager)};

  for (const auto& supported : supported_specs) {
    if (SpecMatches(spec, supported)) {
      return true;
    }
  }

  return false;
}

zx::status<std::unique_ptr<PartitionClient>> CrosDevicePartitioner::AddPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // NOTE: If you update the minimum sizes of partitions, please update the
  // CrosDevicePartitionerTests.InitPartitionTables test.
  size_t minimum_size_bytes = 0;
  switch (spec.partition) {
    case Partition::kZirconA:
      minimum_size_bytes = 64 * kMebibyte;
      break;
    case Partition::kZirconB:
      minimum_size_bytes = 64 * kMebibyte;
      break;
    case Partition::kZirconR:
      // NOTE(abdulla): is_ready_to_pave() is called with SZ_ZX_PART, which requires all kernel
      // partitions to be the same size.
      minimum_size_bytes = 64 * kMebibyte;
      break;
    case Partition::kFuchsiaVolumeManager:
      minimum_size_bytes = 16 * kGibibyte;
      break;
    default:
      ERROR("Cros partitioner cannot add unknown partition type\n");
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  const char* name = PartitionName(spec.partition);
  auto type = CrosPartitionType(spec.partition);
  if (type.is_error()) {
    return type.take_error();
  }
  return gpt_->AddPartition(name, type->data(), minimum_size_bytes, /*optional_reserve_bytes*/ 0);
}

zx::status<std::unique_ptr<PartitionClient>> CrosDevicePartitioner::FindPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  switch (spec.partition) {
    case Partition::kZirconA:
    case Partition::kZirconB:
    case Partition::kZirconR: {
      const auto filter = [&spec](const gpt_partition_t& part) {
        const char* name = PartitionName(spec.partition);
        auto partition_type = CrosPartitionType(spec.partition);
        return partition_type.is_ok() && FilterByTypeAndName(part, partition_type.value(), name);
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
      ERROR("Cros partitioner cannot find unknown partition type\n");
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
}

zx::status<> CrosDevicePartitioner::FinalizePartition(const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // Special partition finalization is only necessary for Zircon partitions.
  if (spec.partition != Partition::kZirconA) {
    return zx::ok();
  }

  // Find the Zircon A kernel partition.
  const std::array<uint8_t, GPT_GUID_LEN> cros_kernel_type = GUID_CROS_KERNEL_VALUE;
  const char* name = PartitionName(Partition::kZirconA);
  const auto filter = [cros_kernel_type, name](const gpt_partition_t& part) {
    return FilterByTypeAndName(part, cros_kernel_type, name);
  };
  auto status = gpt_->FindPartition(filter);
  if (status.is_error()) {
    ERROR("Cannot find %s partition\n", name);
    return status.take_error();
  }
  gpt_partition_t* zircon_a_partition = status->gpt_partition;

  // Find the highest priority kernel partition.
  uint8_t top_priority = 0;
  for (uint32_t i = 0; i < gpt::kPartitionCount; ++i) {
    const gpt_partition_t* part = gpt_->GetGpt()->GetPartition(i);
    if (part == NULL) {
      continue;
    }
    const uint8_t priority = gpt_cros_attr_get_priority(part->flags);
    // Ignore anything not of type CROS KERNEL.
    if (memcmp(part->type, cros_kernel_type.data(), GPT_GUID_LEN)) {
      continue;
    }

    // Ignore ourself.
    if (part == zircon_a_partition) {
      continue;
    }

    if (priority > top_priority) {
      top_priority = priority;
    }
  }

  // Priority for Zircon A set to higher priority than all other kernels.
  if (top_priority == UINT8_MAX) {
    ERROR("Cannot set CrOS partition priority higher than other kernels\n");
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  int ret = gpt_cros_attr_set_priority(&zircon_a_partition->flags,
                                       static_cast<uint8_t>(top_priority + 1));
  if (ret != 0) {
    ERROR("Cannot set CrOS partition priority for ZIRCON-A\n");
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  // TODO(raggi): when other (B/R) partitions are paved, set their priority
  // appropriately as well.

  // Successful set to 'true' to encourage the bootloader to
  // use this partition.
  gpt_cros_attr_set_successful(&zircon_a_partition->flags, true);
  // Maximize the number of attempts to boot this partition before
  // we fall back to a different kernel.
  ret = gpt_cros_attr_set_tries(&zircon_a_partition->flags, 15);
  if (ret != 0) {
    ERROR("Cannot set CrOS partition 'tries' for ZIRCON-A\n");
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  if (auto status = zx::make_status(gpt_->GetGpt()->Sync()); status.is_error()) {
    ERROR("Failed to sync CrOS partition 'tries' for ZIRCON-A\n");
    return status.take_error();
  }

  return zx::ok();
}

zx::status<> CrosDevicePartitioner::WipeFvm() const { return gpt_->WipeFvm(); }

zx::status<> CrosDevicePartitioner::InitPartitionTables() const {
  // Wipe partitions.
  // CrosDevicePartitioner operates on partition names.
  const std::set<std::string_view> partitions_to_wipe{
      GUID_ZIRCON_A_NAME,
      GUID_ZIRCON_B_NAME,
      GUID_ZIRCON_R_NAME,
      GUID_FVM_NAME,
      // These additional partition names are based on the previous naming scheme.
      "ZIRCON-A",
      "ZIRCON-B",
      "ZIRCON-R",
      "fvm",
  };
  auto status = gpt_->WipePartitions([&partitions_to_wipe](const gpt_partition_t& part) {
    char cstring_name[GPT_NAME_LEN] = {};
    utf16_to_cstring(cstring_name, part.name, GPT_NAME_LEN);
    return partitions_to_wipe.find(cstring_name) != partitions_to_wipe.end();
  });
  if (status.is_error()) {
    ERROR("Failed to wipe partitions: %s\n", status.status_string());
    return status.take_error();
  }

  // Add partitions with default content type.
  const std::array<PartitionSpec, 4> partitions_to_add = {
      PartitionSpec(Partition::kZirconA),
      PartitionSpec(Partition::kZirconB),
      PartitionSpec(Partition::kZirconR),
      PartitionSpec(Partition::kFuchsiaVolumeManager),
  };
  for (auto spec : partitions_to_add) {
    auto status = AddPartition(spec);
    if (status.is_error()) {
      ERROR("Failed to create partition \"%s\": %s\n", spec.ToString().c_str(),
            status.status_string());
      return status.take_error();
    }
  }

  LOG("Successfully initialized GPT\n");
  return zx::ok();
}

zx::status<> CrosDevicePartitioner::WipePartitionTables() const {
  return gpt_->WipePartitionTables();
}

zx::status<> CrosDevicePartitioner::ValidatePayload(const PartitionSpec& spec,
                                                    fbl::Span<const uint8_t> data) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  if (IsZirconPartitionSpec(spec)) {
    if (!IsValidChromeOSKernel(data)) {
      return zx::error(ZX_ERR_BAD_STATE);
    }
  }

  return zx::ok();
}

/*====================================================*
 *               FIXED PARTITION MAP                  *
 *====================================================*/

zx::status<std::unique_ptr<DevicePartitioner>> FixedDevicePartitioner::Initialize(
    fbl::unique_fd devfs_root) {
  if (HasSkipBlockDevice(devfs_root)) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  LOG("Successfully initialized FixedDevicePartitioner Device Partitioner\n");
  return zx::ok(new FixedDevicePartitioner(std::move(devfs_root)));
}

bool FixedDevicePartitioner::SupportsPartition(const PartitionSpec& spec) const {
  const PartitionSpec supported_specs[] = {PartitionSpec(paver::Partition::kBootloader),
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

zx::status<std::unique_ptr<PartitionClient>> FixedDevicePartitioner::AddPartition(
    const PartitionSpec& spec) const {
  ERROR("Cannot add partitions to a fixed-map partition device\n");
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<std::unique_ptr<PartitionClient>> FixedDevicePartitioner::FindPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  uint8_t type[GPT_GUID_LEN];

  switch (spec.partition) {
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
    case Partition::kAbrMeta: {
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
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  zx::status<zx::channel> partition = OpenBlockPartition(devfs_root_, nullptr, type, ZX_SEC(5));
  if (partition.is_error()) {
    return partition.take_error();
  }

  return zx::ok(new BlockPartitionClient(std::move(partition.value())));
}

zx::status<> FixedDevicePartitioner::WipeFvm() const {
  const uint8_t fvm_type[GPT_GUID_LEN] = GUID_FVM_VALUE;
  if (auto status = WipeBlockPartition(devfs_root_, nullptr, fvm_type); status.is_error()) {
    ERROR("Failed to wipe FVM.\n");
  } else {
    LOG("Wiped FVM successfully.\n");
  }
  LOG("Immediate reboot strongly recommended\n");
  return zx::ok();
}

zx::status<> FixedDevicePartitioner::InitPartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> FixedDevicePartitioner::WipePartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> FixedDevicePartitioner::ValidatePayload(const PartitionSpec& spec,
                                                     fbl::Span<const uint8_t> data) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  return zx::ok();
}

/*====================================================*
 *                    SHERLOCK                        *
 *====================================================*/

zx::status<std::unique_ptr<DevicePartitioner>> SherlockPartitioner::Initialize(
    fbl::unique_fd devfs_root, const zx::channel& svc_root,
    std::optional<fbl::unique_fd> block_device) {
  auto status = IsBoard(devfs_root, "sherlock");
  if (status.is_error()) {
    return status.take_error();
  }

  auto status_or_gpt =
      GptDevicePartitioner::InitializeGpt(std::move(devfs_root), svc_root, std::move(block_device));
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
      PartitionSpec(paver::Partition::kBootloader, "skip_metadata"),
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

  uint8_t type[GPT_GUID_LEN];

  switch (spec.partition) {
    case Partition::kBootloader: {
      const uint8_t boot0_type[GPT_GUID_LEN] = GUID_EMMC_BOOT1_VALUE;
      auto boot0_part = OpenBlockPartition(gpt_->devfs_root(), nullptr, boot0_type, ZX_SEC(5));
      if (boot0_part.is_error()) {
        return boot0_part.take_error();
      }
      auto boot0 =
          std::make_unique<SherlockBootloaderPartitionClient>(std::move(boot0_part.value()));

      const uint8_t boot1_type[GPT_GUID_LEN] = GUID_EMMC_BOOT2_VALUE;
      auto boot1_part = OpenBlockPartition(gpt_->devfs_root(), nullptr, boot1_type, ZX_SEC(5));
      if (boot1_part.is_error()) {
        return boot1_part.take_error();
      }
      auto boot1 =
          std::make_unique<SherlockBootloaderPartitionClient>(std::move(boot1_part.value()));

      std::vector<std::unique_ptr<PartitionClient>> partitions;
      partitions.push_back(std::move(boot0));
      partitions.push_back(std::move(boot1));

      return zx::ok(std::make_unique<PartitionCopyClient>(std::move(partitions)));
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
    case Partition::kAbrMeta: {
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
      ERROR("Partition type is invalid\n");
      return zx::error(ZX_ERR_INVALID_ARGS);
  }

  const auto filter = [type](const gpt_partition_t& part) {
    return memcmp(part.type, type, GPT_GUID_LEN) == 0;
  };
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
    uint8_t type[GPT_GUID_LEN];
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

  if (auto status = add_partitions(fbl::Span(partitions_to_add)); status.is_error()) {
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

/*====================================================*
 *                SKIP BLOCK SPECIFIC                 *
 *====================================================*/

zx::status<std::unique_ptr<PartitionClient>> SkipBlockDevicePartitioner::FindPartition(
    const uint8_t* type) const {
  zx::status<zx::channel> status = OpenSkipBlockPartition(devfs_root_, type, ZX_SEC(5));
  if (status.is_error()) {
    return status.take_error();
  }

  return zx::ok(new SkipBlockPartitionClient(std::move(status.value())));
}

zx::status<std::unique_ptr<PartitionClient>> SkipBlockDevicePartitioner::FindFvmPartition() const {
  const uint8_t fvm_type[GPT_GUID_LEN] = GUID_FVM_VALUE;
  // FVM partition is managed so it should expose a normal block device.
  zx::status<zx::channel> status = OpenBlockPartition(devfs_root_, nullptr, fvm_type, ZX_SEC(5));
  if (status.is_error()) {
    return status.take_error();
  }

  return zx::ok(new BlockPartitionClient(std::move(status.value())));
}

zx::status<> SkipBlockDevicePartitioner::WipeFvm() const {
  const uint8_t fvm_type[GPT_GUID_LEN] = GUID_FVM_VALUE;
  zx::status<zx::channel> status = OpenBlockPartition(devfs_root_, nullptr, fvm_type, ZX_SEC(3));
  if (status.is_error()) {
    ERROR("Warning: Could not open partition to wipe: %s\n", status.status_string());
    return zx::ok();
  }

  device::Controller::SyncClient block_client(std::move(status.value()));

  auto result = block_client.GetTopologicalPath();
  if (!result.ok()) {
    ERROR("Warning: Could not get name for partition: %s\n", zx_status_get_string(result.status()));
    return zx::error(result.status());
  }
  const auto& response = result.value();
  if (response.result.is_err()) {
    ERROR("Warning: Could not get name for partition: %s\n",
          zx_status_get_string(response.result.err()));
    return zx::error(response.result.err());
  }

  fbl::StringBuffer<PATH_MAX> name_buffer;
  name_buffer.Append(response.result.response().path.data(),
                     static_cast<size_t>(response.result.response().path.size()));

  {
    auto status = zx::make_status(FvmUnbind(devfs_root_, name_buffer.data()));
    if (status.is_error()) {
      // The driver may refuse to bind to a corrupt volume.
      ERROR("Warning: Failed to unbind FVM: %s\n", status.status_string());
    }
  }

  // TODO(39761): Clean this up.
  const char* parent = dirname(name_buffer.data());
  constexpr char kDevRoot[] = "/dev/";
  constexpr size_t kDevRootLen = sizeof(kDevRoot) - 1;
  if (strncmp(parent, kDevRoot, kDevRootLen) != 0) {
    ERROR("Warning: Unrecognized partition name: %s\n", parent);
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  parent += kDevRootLen;

  zx::channel local, remote;
  {
    auto status = zx::make_status(zx::channel::create(0, &local, &remote));
    if (status.is_error()) {
      ERROR("Warning: Failed to create channel pair: %s\n", status.status_string());
      return status.take_error();
    }
  }
  fdio_cpp::UnownedFdioCaller caller(devfs_root_.get());
  {
    auto status =
        zx::make_status(fdio_service_connect_at(caller.borrow_channel(), parent, remote.release()));
    if (status.is_error()) {
      ERROR("Warning: Unable to open block parent device: %s\n", status.status_string());
      return status.take_error();
    }
  }

  block::Ftl::SyncClient client(std::move(local));
  auto result2 = client.Format();

  return zx::make_status(result2.ok() ? result2.value().status : result2.status());
}

bool AstroPartitioner::CanSafelyUpdateLayout(std::shared_ptr<Context> context) {
  // Condition: one successful slot + one unbootable slot
  // Once the layout is updated, it is dangerous to roll back to an older version of system
  // that doesn't have the new logic. Therefore, here we require the A/B state to be in the
  // above state, where it is impossible to roll back to older version.
  std::unique_ptr<PartitionClient> partition_client =
      std::make_unique<AstroSysconfigPartitionClientBuffered>(
          context, sysconfig::SyncClient::PartitionType::kABRMetadata);

  auto status_or_client = abr::AbrPartitionClient::Create(std::move(partition_client));
  if (status_or_client.is_error()) {
    LOG("Failed to create abr-client. Conservatively consider not safe to update layout. %s\n",
        status_or_client.status_string());
    return false;
  }
  std::unique_ptr<abr::Client>& abr_client = status_or_client.value();

  auto status_or_a = abr_client->GetSlotInfo(kAbrSlotIndexA);
  if (status_or_a.is_error()) {
    LOG("Failed to get info for slot A. Conservatively consider not safe to update layout. %s\n",
        status_or_a.status_string());
    return false;
  }
  AbrSlotInfo& slot_a = status_or_a.value();

  auto status_or_b = abr_client->GetSlotInfo(kAbrSlotIndexB);
  if (status_or_b.is_error()) {
    LOG("Failed to get info for slot B. Conservatively consider not safe to update layout. %s\n",
        status_or_b.status_string());
    return false;
  }
  AbrSlotInfo& slot_b = status_or_b.value();

  if (!slot_a.is_marked_successful && !slot_b.is_marked_successful) {
    LOG("No slot is marked successful. Not updating layout.\n")
    return false;
  }

  if (slot_a.is_bootable && slot_b.is_bootable) {
    LOG("The other slot is not marked unbootable. Not updating layout.\n")
    return false;
  }

  return true;
}

zx::status<> AstroPartitioner::InitializeContext(const fbl::unique_fd& devfs_root,
                                                 AbrWearLevelingOption abr_wear_leveling_opt,
                                                 std::shared_ptr<Context> context) {
  return context->Initialize<AstroPartitionerContext>(
      [&]() -> zx::status<std::unique_ptr<AstroPartitionerContext>> {
        std::optional<sysconfig::SyncClient> client;
        if (auto status = zx::make_status(sysconfig::SyncClient::Create(devfs_root, &client));
            status.is_error()) {
          ERROR("Failed to initialize context. %s\n", status.status_string());
          return status.take_error();
        }

        std::unique_ptr<::sysconfig::SyncClientBuffered> sysconfig_client;
        if (abr_wear_leveling_opt == AbrWearLevelingOption::OFF) {
          sysconfig_client = std::make_unique<::sysconfig::SyncClientBuffered>(*std::move(client));
          LOG("Using SyncClientBuffered\n");
        } else if (abr_wear_leveling_opt == AbrWearLevelingOption::ON) {
          sysconfig_client =
              std::make_unique<::sysconfig::SyncClientAbrWearLeveling>(*std::move(client));
          LOG("Using SyncClientAbrWearLeveling\n");
        } else {
          ZX_ASSERT_MSG(false, "Unknown AbrWearLevelingOption %d\n",
                        static_cast<int>(abr_wear_leveling_opt));
        }

        return zx::ok(new AstroPartitionerContext(std::move(sysconfig_client)));
      });
}

zx::status<std::unique_ptr<DevicePartitioner>> AstroPartitioner::Initialize(
    fbl::unique_fd devfs_root, const zx::channel& svc_root, std::shared_ptr<Context> context) {
  auto boot_arg_client = OpenBootArgumentClient(svc_root);
  zx::status<> status = IsBoard(devfs_root, "astro");
  if (status.is_error()) {
    return status.take_error();
  }

  // TODO(47505): Removed this condition and always enabled buffered client when http://fxb/40952
  // is fixed.
  bool enabled_buffered_client =
      boot_arg_client && GetBool(*boot_arg_client, "astro.sysconfig.buffered-client", false);

  if (enabled_buffered_client) {
    // Enable abr wear-leveling only when we see an explicitly defined boot argument
    // "astro.sysconfig.abr-wear-leveling".
    // TODO(47505): Find a proper place to document the parameter.
    AbrWearLevelingOption option =
        boot_arg_client && GetBool(*boot_arg_client, "astro.sysconfig.abr-wear-leveling", false)
            ? AbrWearLevelingOption::ON
            : AbrWearLevelingOption::OFF;

    if (auto status = InitializeContext(devfs_root, option, context); status.is_error()) {
      ERROR("Failed to initialize context. %s\n", status.status_string());
      return status.take_error();
    }

    // CanSafelyUpdateLayout internally acquires the context.lock_. Thus we don't put it in
    // context.Call() or InitializeContext to avoid dead lock.
    if (option == AbrWearLevelingOption::ON && CanSafelyUpdateLayout(context)) {
      if (auto status = context->Call<AstroPartitionerContext>([](auto* ctx) {
            return zx::make_status(ctx->client_->UpdateLayout(
                ::sysconfig::SyncClientAbrWearLeveling::GetAbrWearLevelingSupportedLayout()));
          });
          status.is_error()) {
        return status.take_error();
      }
    }
  }

  LOG("Successfully initialized AstroPartitioner Device Partitioner\n");
  std::unique_ptr<SkipBlockDevicePartitioner> skip_block(
      new SkipBlockDevicePartitioner(std::move(devfs_root)));

  return zx::ok(
      new AstroPartitioner(std::move(skip_block), enabled_buffered_client ? context : nullptr));
}

// Astro bootloader types:
//
// -- default --
// The TPL bootloader image.
//
// Initially we only supported updating the TPL bootloader, so for backwards
// compatibility this must be the default type.
//
// -- "bl2" --
// The BL2 bootloader image.
//
// It's easier to provide the two images separately since on Astro they live
// in different partitions, rather than having to split a combined image.
bool AstroPartitioner::SupportsPartition(const PartitionSpec& spec) const {
  const PartitionSpec supported_specs[] = {PartitionSpec(paver::Partition::kBootloader),
                                           PartitionSpec(paver::Partition::kBootloader, "bl2"),
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

zx::status<std::unique_ptr<PartitionClient>> AstroPartitioner::AddPartition(
    const PartitionSpec& spec) const {
  ERROR("Cannot add partitions to an astro.\n");
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<std::unique_ptr<PartitionClient>> AstroPartitioner::FindPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  switch (spec.partition) {
    case Partition::kBootloader: {
      if (spec.content_type.empty()) {
        // Default type = TPL.
        const uint8_t tpl_type[GPT_GUID_LEN] = GUID_BOOTLOADER_VALUE;
        return skip_block_->FindPartition(tpl_type);
      } else if (spec.content_type == "bl2") {
        const uint8_t bl2_type[GPT_GUID_LEN] = GUID_BL2_VALUE;
        if (auto status = skip_block_->FindPartition(bl2_type); status.is_error()) {
          return status.take_error();
        } else {
          std::unique_ptr<PartitionClient>& bl2_skip_block = status.value();

          // Upgrade this into a more specialized partition client for custom
          // handling required by BL2.
          return zx::ok(new Bl2PartitionClient(bl2_skip_block->GetChannel()));
        }
      }
      // If we get here, we must have added another type to SupportsPartition()
      // without actually implementing it.
      ERROR("Unimplemeneted partition '%s'\n", spec.ToString().c_str());
      return zx::error(ZX_ERR_INTERNAL);
    }
    case Partition::kZirconA: {
      const uint8_t zircon_a_type[GPT_GUID_LEN] = GUID_ZIRCON_A_VALUE;
      return skip_block_->FindPartition(zircon_a_type);
    }
    case Partition::kZirconB: {
      const uint8_t zircon_b_type[GPT_GUID_LEN] = GUID_ZIRCON_B_VALUE;
      return skip_block_->FindPartition(zircon_b_type);
    }
    case Partition::kZirconR: {
      const uint8_t zircon_r_type[GPT_GUID_LEN] = GUID_ZIRCON_R_VALUE;
      return skip_block_->FindPartition(zircon_r_type);
    }
    case Partition::kVbMetaA:
    case Partition::kVbMetaB:
    case Partition::kVbMetaR:
    case Partition::kAbrMeta: {
      const auto type = [&]() {
        switch (spec.partition) {
          case Partition::kVbMetaA:
            return sysconfig::SyncClient::PartitionType::kVerifiedBootMetadataA;
          case Partition::kVbMetaB:
            return sysconfig::SyncClient::PartitionType::kVerifiedBootMetadataB;
          case Partition::kVbMetaR:
            return sysconfig::SyncClient::PartitionType::kVerifiedBootMetadataR;
          case Partition::kAbrMeta:
            return sysconfig::SyncClient::PartitionType::kABRMetadata;
          default:
            break;
        }
        ZX_ASSERT(false);
      }();
      // TODO(47505): Remove the following check and always use buffered client when
      // http://fxb/40952 is fixed.
      if (context_) {
        return zx::ok(new AstroSysconfigPartitionClientBuffered(context_, type));
      } else {
        std::optional<sysconfig::SyncClient> client;
        zx_status_t status = sysconfig::SyncClient::Create(skip_block_->devfs_root(), &client);
        if (status != ZX_OK) {
          return zx::error(status);
        }
        return zx::ok(new SysconfigPartitionClient(*std::move(client), type));
      }
    }
    case Partition::kFuchsiaVolumeManager: {
      return skip_block_->FindFvmPartition();
    }
    default:
      ERROR("partition_type is invalid!\n");
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
}

zx::status<> AstroPartitioner::Flush() const {
  return context_ ? context_->Call<AstroPartitionerContext>(
                        [&](auto* ctx) { return zx::make_status(ctx->client_->Flush()); })
                  : zx::ok();
}

zx::status<> AstroPartitioner::WipeFvm() const { return skip_block_->WipeFvm(); }

zx::status<> AstroPartitioner::InitPartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> AstroPartitioner::WipePartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> AstroPartitioner::ValidatePayload(const PartitionSpec& spec,
                                               fbl::Span<const uint8_t> data) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  return zx::ok();
}

zx::status<std::unique_ptr<DevicePartitioner>> As370Partitioner::Initialize(
    fbl::unique_fd devfs_root) {
  zx::status<> status = IsBoard(devfs_root, "visalia");
  if (status.is_error()) {
    return status.take_error();
  }
  LOG("Successfully initialized As370Partitioner Device Partitioner\n");

  std::unique_ptr<SkipBlockDevicePartitioner> skip_block(
      new SkipBlockDevicePartitioner(std::move(devfs_root)));
  return zx::ok(new As370Partitioner(std::move(skip_block)));
}

bool As370Partitioner::SupportsPartition(const PartitionSpec& spec) const {
  const PartitionSpec supported_specs[] = {
      PartitionSpec(paver::Partition::kBootloader), PartitionSpec(paver::Partition::kZirconA),
      PartitionSpec(paver::Partition::kZirconB), PartitionSpec(paver::Partition::kZirconR),
      PartitionSpec(paver::Partition::kFuchsiaVolumeManager)};

  for (const auto& supported : supported_specs) {
    if (SpecMatches(spec, supported)) {
      return true;
    }
  }

  return false;
}

zx::status<std::unique_ptr<PartitionClient>> As370Partitioner::AddPartition(
    const PartitionSpec& spec) const {
  ERROR("Cannot add partitions to an as370.\n");
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<std::unique_ptr<PartitionClient>> As370Partitioner::FindPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  switch (spec.partition) {
    case Partition::kBootloader: {
      const uint8_t bootloader_type[GPT_GUID_LEN] = GUID_BOOTLOADER_VALUE;
      return skip_block_->FindPartition(bootloader_type);
    }
    case Partition::kZirconA: {
      const uint8_t zircon_a_type[GPT_GUID_LEN] = GUID_ZIRCON_A_VALUE;
      return skip_block_->FindPartition(zircon_a_type);
    }
    case Partition::kZirconB: {
      const uint8_t zircon_b_type[GPT_GUID_LEN] = GUID_ZIRCON_B_VALUE;
      return skip_block_->FindPartition(zircon_b_type);
    }
    case Partition::kZirconR: {
      const uint8_t zircon_r_type[GPT_GUID_LEN] = GUID_ZIRCON_R_VALUE;
      return skip_block_->FindPartition(zircon_r_type);
    }
    case Partition::kFuchsiaVolumeManager: {
      return skip_block_->FindFvmPartition();
    }
    default:
      ERROR("partition_type is invalid!\n");
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
}

zx::status<> As370Partitioner::WipeFvm() const { return skip_block_->WipeFvm(); }

zx::status<> As370Partitioner::InitPartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> As370Partitioner::WipePartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> As370Partitioner::ValidatePayload(const PartitionSpec& spec,
                                               fbl::Span<const uint8_t> data) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  return zx::ok();
}

}  // namespace paver
