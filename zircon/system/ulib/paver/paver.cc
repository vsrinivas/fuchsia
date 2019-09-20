// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "paver.h"

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/hardware/skipblock/llcpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <libgen.h>
#include <stddef.h>
#include <string.h>
#include <zircon/device/block.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <utility>

#include <block-client/cpp/client.h>
#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <zxcrypt/fdio-volume.h>

#include "fvm.h"
#include "pave-logging.h"
#include "stream-reader.h"
#include "vmo-reader.h"

#define ZXCRYPT_DRIVER_LIB "/boot/driver/zxcrypt.so"

namespace paver {
namespace {

namespace skipblock = ::llcpp::fuchsia::hardware::skipblock;

using ::llcpp::fuchsia::paver::Asset;
using ::llcpp::fuchsia::paver::Configuration;

Partition PartitionType(Configuration configuration, Asset asset) {
  switch (asset) {
    case Asset::KERNEL: {
      switch (configuration) {
        case Configuration::A:
          return Partition::kZirconA;
        case Configuration::B:
          return Partition::kZirconB;
        case Configuration::RECOVERY:
          return Partition::kZirconR;
      };
      break;
    }
    case Asset::VERIFIED_BOOT_METADATA: {
      switch (configuration) {
        case Configuration::A:
          return Partition::kVbMetaA;
        case Configuration::B:
          return Partition::kVbMetaB;
        case Configuration::RECOVERY:
          return Partition::kVbMetaR;
      };
      break;
    }
  };
  return Partition::kUnknown;
}

// Best effort attempt to see if payload contents match what is already inside
// of the partition.
bool CheckIfSame(fbl::Function<zx_status_t(const zx::vmo&)> read_to_vmo, const zx::vmo& vmo,
                 size_t vmo_size) {
  zx::vmo read_vmo;
  auto status = zx::vmo::create(fbl::round_up(vmo_size, ZX_PAGE_SIZE), 0, &read_vmo);
  if (status != ZX_OK) {
    ERROR("Failed to create VMO: %s\n", zx_status_get_string(status));
    return false;
  }

  if ((status = read_to_vmo(read_vmo)) != ZX_OK) {
    return false;
  }

  fzl::VmoMapper first_mapper;
  fzl::VmoMapper second_mapper;

  status = first_mapper.Map(vmo, 0, 0, ZX_VM_PERM_READ);
  if (status != ZX_OK) {
    ERROR("Error mapping vmo: %s\n", zx_status_get_string(status));
    return false;
  }

  status = second_mapper.Map(read_vmo, 0, 0, ZX_VM_PERM_READ);
  if (status != ZX_OK) {
    ERROR("Error mapping vmo: %s\n", zx_status_get_string(status));
    return false;
  }

  return memcmp(first_mapper.start(), second_mapper.start(), vmo_size) == 0;
}

// Writes a raw (non-FVM) partition to a block device from a VMO.
zx_status_t WriteVmoToBlock(const zx::vmo& vmo, size_t vmo_size, const fbl::unique_fd& partition_fd,
                            uint32_t block_size_bytes) {
  ZX_ASSERT(vmo_size % block_size_bytes == 0);

  auto read_to_vmo = [&](const zx::vmo& vmo) -> zx_status_t {
    vmoid_t vmoid;
    block_client::Client client;
    zx_status_t status = RegisterFastBlockIo(partition_fd, vmo, &vmoid, &client);
    if (status != ZX_OK) {
      ERROR("Cannot register fast block I/O\n");
      return status;
    }

    block_fifo_request_t request;
    request.group = 0;
    request.vmoid = vmoid;
    request.opcode = BLOCKIO_READ;

    uint64_t length = vmo_size / block_size_bytes;
    if (length > UINT32_MAX) {
      ERROR("Error reading partition data: Too large\n");
      return ZX_ERR_OUT_OF_RANGE;
    }
    request.length = static_cast<uint32_t>(length);
    request.vmo_offset = 0;
    request.dev_offset = 0;

    if ((status = client.Transaction(&request, 1)) != ZX_OK) {
      ERROR("Error reading partition data: %s\n", zx_status_get_string(status));
      return status;
    }
    return ZX_OK;
  };

  if (CheckIfSame(read_to_vmo, vmo, vmo_size)) {
    LOG("Skipping write as partition contents match payload.\n");
    return ZX_OK;
  }

  vmoid_t vmoid;
  block_client::Client client;
  zx_status_t status = RegisterFastBlockIo(partition_fd, vmo, &vmoid, &client);
  if (status != ZX_OK) {
    ERROR("Cannot register fast block I/O\n");
    return status;
  }

  block_fifo_request_t request;
  request.group = 0;
  request.vmoid = vmoid;
  request.opcode = BLOCKIO_WRITE;

  uint64_t length = vmo_size / block_size_bytes;
  if (length > UINT32_MAX) {
    ERROR("Error writing partition data: Too large\n");
    return ZX_ERR_OUT_OF_RANGE;
  }
  request.length = static_cast<uint32_t>(length);
  request.vmo_offset = 0;
  request.dev_offset = 0;

  if ((status = client.Transaction(&request, 1)) != ZX_OK) {
    ERROR("Error writing partition data: %s\n", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

// Writes a raw (non-FVM) partition to a skip-block device from a VMO.
zx_status_t WriteVmoToSkipBlock(const zx::vmo& vmo, size_t vmo_size,
                                const fzl::UnownedFdioCaller& caller, uint32_t block_size_bytes) {
  ZX_ASSERT(vmo_size % block_size_bytes == 0);

  auto read_to_vmo = [&](const zx::vmo& vmo) -> zx_status_t {
    zx_status_t status;
    zx::vmo dup;
    if ((status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup)) != ZX_OK) {
      ERROR("Couldn't duplicate buffer vmo\n");
      return status;
    }

    skipblock::ReadWriteOperation operation = {
        .vmo = std::move(dup),
        .vmo_offset = 0,
        .block = 0,
        .block_count = static_cast<uint32_t>(vmo_size / block_size_bytes),
    };

    auto result = skipblock::SkipBlock::Call::Read(caller.channel(), std::move(operation));
    status = result.ok() ? result.value().status : result.status();
    if (!result.ok()) {
      ERROR("Error reading partition data: %s\n", zx_status_get_string(status));
      return status;
    }
    return ZX_OK;
  };

  if (CheckIfSame(read_to_vmo, vmo, vmo_size)) {
    LOG("Skipping write as partition contents match payload.\n");
    return ZX_OK;
  }

  zx_status_t status;
  zx::vmo dup;
  if ((status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup)) != ZX_OK) {
    ERROR("Couldn't duplicate buffer vmo\n");
    return status;
  }

  skipblock::ReadWriteOperation operation = {
      .vmo = std::move(dup),
      .vmo_offset = 0,
      .block = 0,
      .block_count = static_cast<uint32_t>(vmo_size / block_size_bytes),
  };

  auto result = skipblock::SkipBlock::Call::Write(caller.channel(), std::move(operation));
  status = result.ok() ? result.value().status : result.status();
  if (status != ZX_OK) {
    ERROR("Error writing partition data: %s\n", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

#if 0
// Checks first few bytes of buffer to ensure it is a ZBI.
// Also validates architecture in kernel header matches the target.
bool ValidateKernelZbi(const uint8_t* buffer, size_t size, Arch arch) {
    const auto payload = reinterpret_cast<const zircon_kernel_t*>(buffer);
    const uint32_t expected_kernel =
        (arch == Arch::X64) ? ZBI_TYPE_KERNEL_X64 : ZBI_TYPE_KERNEL_ARM64;

    const auto crc_valid = [](const zbi_header_t* hdr) {
        const uint32_t crc = crc32(0, reinterpret_cast<const uint8_t*>(hdr + 1), hdr->length);
        return hdr->crc32 == crc;
    };

    return size >= sizeof(zircon_kernel_t) &&
           // Container header
           payload->hdr_file.type == ZBI_TYPE_CONTAINER &&
           payload->hdr_file.extra == ZBI_CONTAINER_MAGIC &&
           (payload->hdr_file.length - offsetof(zircon_kernel_t, hdr_kernel)) <= size &&
           payload->hdr_file.magic == ZBI_ITEM_MAGIC &&
           payload->hdr_file.flags == ZBI_FLAG_VERSION &&
           payload->hdr_file.crc32 == ZBI_ITEM_NO_CRC32 &&
           // Kernel header
           payload->hdr_kernel.type == expected_kernel &&
           (payload->hdr_kernel.length - offsetof(zircon_kernel_t, data_kernel)) <= size &&
           payload->hdr_kernel.magic == ZBI_ITEM_MAGIC &&
           (payload->hdr_kernel.flags & ZBI_FLAG_VERSION) == ZBI_FLAG_VERSION &&
           ((payload->hdr_kernel.flags & ZBI_FLAG_CRC32)
                ? crc_valid(&payload->hdr_kernel)
                : payload->hdr_kernel.crc32 == ZBI_ITEM_NO_CRC32);
}

// Parses a partition and validates that it matches the expected format.
zx_status_t ValidateKernelPayload(const fzl::ResizeableVmoMapper& mapper, size_t vmo_size,
                                  Partition partition_type, Arch arch) {
    // TODO(surajmalhotra): Re-enable this as soon as we have a good way to
    // determine whether the payload is signed or not. (Might require bootserver
    // changes).
    if (false) {
        const auto* buffer = reinterpret_cast<uint8_t*>(mapper.start());
        switch (partition_type) {
        case Partition::kZirconA:
        case Partition::kZirconB:
        case Partition::kZirconR:
            if (!ValidateKernelZbi(buffer, vmo_size, arch)) {
                ERROR("Invalid ZBI payload!");
                return ZX_ERR_BAD_STATE;
            }
            break;

        default:
            // TODO(surajmalhotra): Validate non-zbi payloads as well.
            LOG("Skipping validation as payload is not a ZBI\n");
            break;
        }
    }

    return ZX_OK;
}
#endif

zx_status_t FvmPave(const DevicePartitioner& partitioner,
                    fbl::unique_ptr<fvm::ReaderInterface> payload) {
  LOG("Paving partition.\n");

  constexpr auto partition_type = Partition::kFuchsiaVolumeManager;
  zx_status_t status;
  fbl::unique_fd partition_fd;
  if ((status = partitioner.FindPartition(partition_type, &partition_fd)) != ZX_OK) {
    if (status != ZX_ERR_NOT_FOUND) {
      ERROR("Failure looking for partition: %s\n", zx_status_get_string(status));
      return status;
    }

    LOG("Coud not find \"%s\" Partition on device. Attemping to add new partition\n",
        PartitionName(partition_type));

    if ((status = partitioner.AddPartition(partition_type, &partition_fd)) != ZX_OK) {
      ERROR("Failure creating partition: %s\n", zx_status_get_string(status));
      return status;
    }
  } else {
    LOG("Partition already exists\n");
  }

  if (partitioner.UseSkipBlockInterface()) {
    LOG("Attempting to format FTL...\n");
    status = partitioner.WipeFvm();
    if (status != ZX_OK) {
      ERROR("Failed to format FTL: %s\n", zx_status_get_string(status));
    } else {
      LOG("Formatted successfully!\n");
    }
  }
  LOG("Streaming partitions...\n");
  if ((status = FvmStreamPartitions(std::move(partition_fd), std::move(payload))) != ZX_OK) {
    ERROR("Failed to stream partitions: %s\n", zx_status_get_string(status));
    return status;
  }
  LOG("Completed successfully\n");
  return ZX_OK;
}

// Paves an image onto the disk.
zx_status_t PartitionPave(const DevicePartitioner& partitioner, zx::vmo payload_vmo,
                          size_t payload_size, Partition partition_type) {
  LOG("Paving partition.\n");

  zx_status_t status;
  fbl::unique_fd partition_fd;
  if ((status = partitioner.FindPartition(partition_type, &partition_fd)) != ZX_OK) {
    if (status != ZX_ERR_NOT_FOUND) {
      ERROR("Failure looking for partition: %s\n", zx_status_get_string(status));
      return status;
    }

    LOG("Coud not find \"%s\" Partition on device. Attemping to add new partition\n",
        PartitionName(partition_type));

    if ((status = partitioner.AddPartition(partition_type, &partition_fd)) != ZX_OK) {
      ERROR("Failure creating partition: %s\n", zx_status_get_string(status));
      return status;
    }
  } else {
    LOG("Partition already exists\n");
  }

  uint32_t block_size_bytes;
  if ((status = partitioner.GetBlockSize(partition_fd, &block_size_bytes)) != ZX_OK) {
    ERROR("Couldn't get partition block size\n");
    return status;
  }

  // Pad payload with 0s to make it block size aligned.
  if (payload_size % block_size_bytes != 0) {
    const size_t remaining_bytes = block_size_bytes - (payload_size % block_size_bytes);
    size_t vmo_size;
    if ((status = payload_vmo.get_size(&vmo_size)) != ZX_OK) {
      ERROR("Couldn't get vmo size\n");
      return status;
    }
    // Grow VMO if it's too small.
    if (vmo_size < payload_size + remaining_bytes) {
      const auto new_size = fbl::round_up(payload_size + remaining_bytes, ZX_PAGE_SIZE);
      status = payload_vmo.set_size(new_size);
      if (status != ZX_OK) {
        ERROR("Couldn't grow vmo\n");
        return status;
      }
    }
    auto buffer = std::make_unique<uint8_t[]>(remaining_bytes);
    memset(buffer.get(), 0, remaining_bytes);
    status = payload_vmo.write(buffer.get(), payload_size, remaining_bytes);
    if (status != ZX_OK) {
      ERROR("Failed to write padding to vmo\n");
      return status;
    }
    payload_size += remaining_bytes;
  }

  if (partitioner.UseSkipBlockInterface()) {
    fzl::UnownedFdioCaller caller(partition_fd.get());
    status = WriteVmoToSkipBlock(payload_vmo, payload_size, caller, block_size_bytes);
  } else {
    status = WriteVmoToBlock(payload_vmo, payload_size, partition_fd, block_size_bytes);
  }
  if (status != ZX_OK) {
    ERROR("Failed to write partition to block\n");
    return status;
  }

  if ((status = partitioner.FinalizePartition(partition_type)) != ZX_OK) {
    ERROR("Failed to finalize partition\n");
    return status;
  }

  LOG("Completed successfully\n");
  return ZX_OK;
}

}  // namespace

bool Paver::InitializePartitioner() {
  if (!partitioner_) {
    // Use global devfs if one wasn't injected via set_devfs_root.
    if (!devfs_root_) {
      devfs_root_ = fbl::unique_fd(open("/dev", O_RDONLY));
    }
#if defined(__x86_64__)
    Arch arch = Arch::kX64;
#elif defined(__aarch64__)
    Arch arch = Arch::kArm64;
#else
#error "Unknown arch"
#endif
    partitioner_ = DevicePartitioner::Create(devfs_root_.duplicate(), arch);
    if (!partitioner_) {
      ERROR("Unable to initialize a partitioner.\n");
      return false;
    }
  }
  return true;
}

void Paver::WriteAsset(Configuration configuration, Asset asset,
                       ::llcpp::fuchsia::mem::Buffer payload, WriteAssetCompleter::Sync completer) {
  if (!InitializePartitioner()) {
    completer.Reply(ZX_ERR_BAD_STATE);
    return;
  }
  completer.Reply(PartitionPave(*partitioner_, std::move(payload.vmo), payload.size,
                                PartitionType(configuration, asset)));
}

void Paver::WriteVolumes(zx::channel payload_stream, WriteVolumesCompleter::Sync completer) {
  if (!InitializePartitioner()) {
    completer.Reply(ZX_ERR_BAD_STATE);
  }

  std::unique_ptr<StreamReader> reader;
  auto status = StreamReader::Create(std::move(payload_stream), &reader);
  if (status != ZX_OK) {
    ERROR("Unable to create stream.\n");
    completer.Reply(status);
    return;
  }
  completer.Reply(FvmPave(*partitioner_, std::move(reader)));
}

void Paver::WriteBootloader(::llcpp::fuchsia::mem::Buffer payload,
                            WriteBootloaderCompleter::Sync completer) {
  if (!InitializePartitioner()) {
    completer.Reply(ZX_ERR_BAD_STATE);
    return;
  }
  completer.Reply(
      PartitionPave(*partitioner_, std::move(payload.vmo), payload.size, Partition::kBootloader));
}

void Paver::WriteDataFile(fidl::StringView filename, ::llcpp::fuchsia::mem::Buffer payload,
                          WriteDataFileCompleter::Sync completer) {
  const char* mount_path = "/volume/data";
  const uint8_t data_guid[] = GUID_DATA_VALUE;
  char minfs_path[PATH_MAX] = {0};
  char path[PATH_MAX] = {0};
  zx_status_t status = ZX_OK;

  fbl::unique_fd part_fd(open_partition(nullptr, data_guid, ZX_SEC(1), path));
  if (!part_fd) {
    ERROR("DATA partition not found in FVM\n");
    completer.Reply(ZX_ERR_NOT_FOUND);
    return;
  }

  auto disk_format = detect_disk_format(part_fd.get());
  fbl::unique_fd mountpoint_dev_fd;
  // By the end of this switch statement, mountpoint_dev_fd needs to be an
  // open handle to the block device that we want to mount at mount_path.
  switch (disk_format) {
    case DISK_FORMAT_MINFS:
      // If the disk we found is actually minfs, we can just use the block
      // device path we were given by open_partition.
      strncpy(minfs_path, path, PATH_MAX);
      mountpoint_dev_fd.reset(open(minfs_path, O_RDWR));
      break;

    case DISK_FORMAT_ZXCRYPT: {
      fbl::unique_ptr<zxcrypt::FdioVolume> zxc_volume;
      uint8_t slot = 0;
      // Use global devfs if one wasn't injected via set_devfs_root.
      if (!devfs_root_) {
        devfs_root_ = fbl::unique_fd(open("/dev", O_RDONLY));
      }
      if ((status = zxcrypt::FdioVolume::UnlockWithDeviceKey(
               std::move(part_fd), devfs_root_.duplicate(), static_cast<zxcrypt::key_slot_t>(slot),
               &zxc_volume)) != ZX_OK) {
        ERROR("Couldn't unlock zxcrypt volume: %s\n", zx_status_get_string(status));
        completer.Reply(status);
        return;
      }

      // Most of the time we'll expect the volume to actually already be
      // unsealed, because we created it and unsealed it moments ago to
      // format minfs.
      if ((status = zxc_volume->Open(zx::sec(0), &mountpoint_dev_fd)) == ZX_OK) {
        // Already unsealed, great, early exit.
        break;
      }

      // Ensure zxcrypt volume manager is bound.
      zx::channel zxc_manager_chan;
      if ((status = zxc_volume->OpenManager(zx::sec(5),
                                            zxc_manager_chan.reset_and_get_address())) != ZX_OK) {
        ERROR("Couldn't open zxcrypt volume manager: %s\n", zx_status_get_string(status));
        completer.Reply(status);
        return;
      }

      // Unseal.
      zxcrypt::FdioVolumeManager zxc_manager(std::move(zxc_manager_chan));
      if ((status = zxc_manager.UnsealWithDeviceKey(slot)) != ZX_OK) {
        ERROR("Couldn't unseal zxcrypt volume: %s\n", zx_status_get_string(status));
        completer.Reply(status);
        return;
      }

      // Wait for the device to appear, and open it.
      if ((status = zxc_volume->Open(zx::sec(5), &mountpoint_dev_fd)) != ZX_OK) {
        ERROR("Couldn't open block device atop unsealed zxcrypt volume: %s\n",
              zx_status_get_string(status));
        completer.Reply(status);
        return;
      }
    } break;

    default:
      ERROR("unsupported disk format at %s\n", path);
      completer.Reply(ZX_ERR_NOT_SUPPORTED);
      return;
  }

  mount_options_t opts(default_mount_options);
  opts.create_mountpoint = true;
  if ((status = mount(mountpoint_dev_fd.get(), mount_path, DISK_FORMAT_MINFS, &opts,
                      launch_logs_async)) != ZX_OK) {
    ERROR("mount error: %s\n", zx_status_get_string(status));
    completer.Reply(status);
    return;
  }

  int filename_size = static_cast<int>(filename.size());

  // mkdir any intermediate directories between mount_path and basename(filename).
  snprintf(path, sizeof(path), "%s/%.*s", mount_path, filename_size, filename.data());
  size_t cur = strlen(mount_path);
  size_t max = strlen(path) - strlen(basename(path));
  // note: the call to basename above modifies path, so it needs reconstruction.
  snprintf(path, sizeof(path), "%s/%.*s", mount_path, filename_size, filename.data());
  while (cur < max) {
    ++cur;
    if (path[cur] == '/') {
      path[cur] = 0;
      // errors ignored, let the open() handle that later.
      mkdir(path, 0700);
      path[cur] = '/';
    }
  }

  // We append here, because the primary use case here is to send SSH keys
  // which can be appended, but we may want to revisit this choice for other
  // files in the future.
  {
    uint8_t buf[8192];
    fbl::unique_fd kfd(open(path, O_CREAT | O_WRONLY | O_APPEND, 0600));
    if (!kfd) {
      umount(mount_path);
      ERROR("open %.*s error: %s\n", filename_size, filename.data(), strerror(errno));
      completer.Reply(ZX_ERR_IO);
      return;
    }
    VmoReader reader(std::move(payload));
    size_t actual;
    while ((status = reader.Read(buf, sizeof(buf), &actual)) == ZX_OK && actual > 0) {
      if (write(kfd.get(), buf, actual) != static_cast<ssize_t>(actual)) {
        umount(mount_path);
        ERROR("write %.*s error: %s\n", filename_size, filename.data(), strerror(errno));
        completer.Reply(ZX_ERR_IO);
        return;
      }
    }
    fsync(kfd.get());
  }

  if ((status = umount(mount_path)) != ZX_OK) {
    ERROR("unmount %s failed: %s\n", mount_path, zx_status_get_string(status));
    completer.Reply(status);
    return;
  }

  LOG("Wrote %.*s\n", filename_size, filename.data());
  completer.Reply(ZX_OK);
}

void Paver::WipeVolumes(zx::channel block_device, WipeVolumesCompleter::Sync completer) {
  partitioner_.reset();
  // Use global devfs if one wasn't injected via set_devfs_root.
  if (!devfs_root_) {
    devfs_root_ = fbl::unique_fd(open("/dev", O_RDONLY));
  }
#if defined(__x86_64__)
  Arch arch = Arch::kX64;
#elif defined(__aarch64__)
  Arch arch = Arch::kArm64;
#else
#error "Unknown arch"
#endif
  auto partitioner =
      DevicePartitioner::Create(devfs_root_.duplicate(), arch, std::move(block_device));
  if (!partitioner) {
    ERROR("Unable to initialize a partitioner.\n");
    completer.Reply(ZX_ERR_BAD_STATE);
    return;
  }

  completer.Reply(partitioner->WipeFvm());
}

void Paver::InitializePartitionTables(zx::channel block_device,
                                      InitializePartitionTablesCompleter::Sync completer) {
  // Use global devfs if one wasn't injected via set_devfs_root.
  if (!devfs_root_) {
    devfs_root_ = fbl::unique_fd(open("/dev", O_RDONLY));
  }
#if defined(__x86_64__)
  Arch arch = Arch::kX64;
#elif defined(__aarch64__)
  Arch arch = Arch::kArm64;
#else
#error "Unknown arch"
#endif
  auto partitioner =
      DevicePartitioner::Create(devfs_root_.duplicate(), arch, std::move(block_device));
  if (!partitioner) {
    ERROR("Unable to initialize a partitioner.\n");
    completer.Reply(ZX_ERR_BAD_STATE);
    return;
  }

  constexpr auto partition_type = Partition::kFuchsiaVolumeManager;
  zx_status_t status;
  fbl::unique_fd partition_fd;
  if ((status = partitioner->FindPartition(partition_type, &partition_fd)) != ZX_OK) {
    if (status != ZX_ERR_NOT_FOUND) {
      ERROR("Failure looking for partition: %s\n", zx_status_get_string(status));
      completer.Reply(status);
      return;
    }

    LOG("Could not find \"%s\" Partition on device. Attemping to add new partition\n",
        PartitionName(partition_type));

    if ((status = partitioner->AddPartition(partition_type, &partition_fd)) != ZX_OK) {
      ERROR("Failure creating partition: %s\n", zx_status_get_string(status));
      completer.Reply(status);
      return;
    }
  }
  partitioner_ = std::move(partitioner);
  LOG("Successfully initialized gpt.\n");
  completer.Reply(ZX_OK);
}

void Paver::WipePartitionTables(zx::channel block_device,
                                WipePartitionTablesCompleter::Sync completer) {
  partitioner_.reset();
  // Use global devfs if one wasn't injected via set_devfs_root.
  if (!devfs_root_) {
    devfs_root_ = fbl::unique_fd(open("/dev", O_RDONLY));
  }
#if defined(__x86_64__)
  Arch arch = Arch::kX64;
#elif defined(__aarch64__)
  Arch arch = Arch::kArm64;
#else
#error "Unknown arch"
#endif
  auto partitioner =
      DevicePartitioner::Create(devfs_root_.duplicate(), arch, std::move(block_device));
  if (!partitioner) {
    ERROR("Unable to initialize a partitioner.\n");
    completer.Reply(ZX_ERR_BAD_STATE);
    return;
  }

  completer.Reply(partitioner->WipePartitionTables());
}

}  //  namespace paver
