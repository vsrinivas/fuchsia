// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fvm.h"

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.partition/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/channel.h>
#include <lib/zx/fifo.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <cstddef>
#include <memory>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <ramdevice-client/ramdisk.h>
#include <safemath/safe_math.h>

#include "pave-logging.h"
#include "src/lib/storage/block_client/cpp/client.h"
#include "src/lib/storage/fs_management/cpp/fvm.h"
#include "src/lib/uuid/uuid.h"
#include "src/security/lib/zxcrypt/client.h"
#include "src/storage/fshost/constants.h"
#include "src/storage/fvm/format.h"
#include "src/storage/fvm/fvm_sparse.h"

namespace paver {
namespace {

namespace block = fuchsia_hardware_block;
namespace partition = fuchsia_hardware_block_partition;
namespace volume = fuchsia_hardware_block_volume;
namespace device = fuchsia_device;

using fuchsia_hardware_block_volume::wire::VolumeManagerInfo;

// The number of additional slices a partition will need to become
// zxcrypt'd.
//
// TODO(aarongreen): Replace this with a value supplied by ulib/zxcrypt.
constexpr size_t kZxcryptExtraSlices = 1;

// Looks up the topological path of a device.
// |buf| is the buffer the path will be written to.  |buf_len| is the total
// capcity of the buffer, including space for a null byte.
// Upon success, |buf| will contain the null-terminated topological path.
zx_status_t GetTopoPathFromFd(const fbl::unique_fd& fd, char* buf, size_t buf_len) {
  fdio_cpp::UnownedFdioCaller caller(fd.get());
  auto resp = fidl::WireCall(caller.borrow_as<fuchsia_device::Controller>())->GetTopologicalPath();
  if (!resp.ok()) {
    return resp.status();
  }
  if (resp->is_error()) {
    return resp->error_value();
  }

  auto& response = *resp->value();
  strncpy(buf, response.path.data(), std::min(buf_len, response.path.size()));
  buf[response.path.size()] = '\0';
  return ZX_OK;
}

// Confirm that the file descriptor to the underlying partition exists within an
// FVM, not, for example, a GPT or MBR.
//
// |out| is true if |fd| is a VPartition, else false.
zx_status_t FvmIsVirtualPartition(const fbl::unique_fd& fd, bool* out) {
  char path[PATH_MAX];
  zx_status_t status = GetTopoPathFromFd(fd, path, sizeof(path));
  if (status != ZX_OK) {
    return ZX_ERR_IO;
  }

  *out = strstr(path, "fvm") != nullptr;
  return ZX_OK;
}

// Describes the state of a partition actively being written
// out to disk.
struct PartitionInfo {
  fvm::PartitionDescriptor* pd = nullptr;
  fvm::PartitionDescriptor aligned_pd = {};
  fbl::unique_fd new_part;
  bool active = false;
};

ptrdiff_t GetExtentOffset(size_t extent) {
  return safemath::checked_cast<ptrdiff_t>(sizeof(fvm::PartitionDescriptor) +
                                           extent * sizeof(fvm::ExtentDescriptor));
}

fvm::ExtentDescriptor GetExtent(fvm::PartitionDescriptor* pd, size_t extent) {
  fvm::ExtentDescriptor descriptor = {};
  const auto* descriptor_ptr = reinterpret_cast<uint8_t*>(pd) + GetExtentOffset(extent);
  memcpy(&descriptor, descriptor_ptr, sizeof(fvm::ExtentDescriptor));
  return descriptor;
}

// Registers a FIFO
zx_status_t RegisterFastBlockIo(const fbl::unique_fd& fd, const zx::vmo& vmo, vmoid_t* out_vmoid,
                                std::unique_ptr<block_client::Client>* out_client) {
  fdio_cpp::UnownedFdioCaller caller(fd.get());

  auto result = fidl::WireCall(caller.borrow_as<block::Block>())->GetFifo();
  if (!result.ok()) {
    return result.status();
  }
  auto& response = result.value();
  if (response.status != ZX_OK) {
    return response.status;
  }

  zx::vmo dup;
  if (vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup) != ZX_OK) {
    ERROR("Couldn't duplicate buffer vmo\n");
    return ZX_ERR_IO;
  }

  auto result2 = fidl::WireCall(caller.borrow_as<block::Block>())->AttachVmo(std::move(dup));
  if (result2.status() != ZX_OK) {
    return result2.status();
  }
  const auto& response2 = result2.value();
  if (response2.status != ZX_OK) {
    return response2.status;
  }

  *out_vmoid = response2.vmoid->id;
  *out_client = std::make_unique<block_client::Client>(std::move(response.fifo));
  return ZX_OK;
}

zx_status_t FlushClient(block_client::Client* client) {
  block_fifo_request_t request;
  request.group = 0;
  request.vmoid = block::wire::kVmoidInvalid;
  request.opcode = BLOCKIO_FLUSH;
  request.length = 0;
  request.vmo_offset = 0;
  request.dev_offset = 0;

  return client->Transaction(&request, 1);
}

// Stream an FVM partition to disk.
zx_status_t StreamFvmPartition(fvm::SparseReader* reader, PartitionInfo* part,
                               const fzl::VmoMapper& mapper, block_client::Client& client,
                               size_t block_size, block_fifo_request_t* request) {
  size_t slice_size = reader->Image()->slice_size;
  const size_t vmo_cap = mapper.size();
  for (size_t e = 0; e < part->aligned_pd.extent_count; e++) {
    LOG("Writing extent %zu... \n", e);
    fvm::ExtentDescriptor ext = GetExtent(part->pd, e);
    size_t offset = ext.slice_start * slice_size;
    size_t bytes_left = ext.extent_length;

    // Write real data
    while (bytes_left > 0) {
      size_t actual;
      zx_status_t status = reader->ReadData(reinterpret_cast<uint8_t*>(mapper.start()),
                                            std::min(bytes_left, vmo_cap), &actual);
      if (status != ZX_OK) {
        ERROR("Error reading extent data with %zu bytes of %zu remaining: %s\n", bytes_left,
              ext.extent_length, zx_status_get_string(status));
        return status;
      }

      const size_t vmo_sz = actual;
      bytes_left -= actual;

      if (vmo_sz == 0) {
        ERROR("Read nothing from src_fd; %zu bytes left\n", bytes_left);
        return ZX_ERR_IO;
      }
      if (vmo_sz % block_size != 0) {
        ERROR("Cannot write non-block size multiple: %zu\n", vmo_sz);
        return ZX_ERR_IO;
      }

      uint64_t length = vmo_sz / block_size;
      if (length > UINT32_MAX) {
        ERROR("Error writing partition: Too large\n");
        return ZX_ERR_OUT_OF_RANGE;
      }
      request->length = static_cast<uint32_t>(length);
      request->vmo_offset = 0;
      request->dev_offset = offset / block_size;

      if (zx_status_t status = client.Transaction(request, 1); status != ZX_OK) {
        ERROR("Error writing partition data\n");
        return status;
      }

      offset += vmo_sz;
    }

    // Write trailing zeroes (which are implied, but were omitted from
    // transfer).
    bytes_left = (ext.slice_count * slice_size) - ext.extent_length;
    if (bytes_left > 0) {
      LOG("%zu bytes written, %zu zeroes left\n", ext.extent_length, bytes_left);
      memset(mapper.start(), 0, vmo_cap);
    }
    while (bytes_left > 0) {
      uint64_t length = std::min(bytes_left, vmo_cap) / block_size;
      if (length > UINT32_MAX) {
        ERROR("Error writing trailing zeroes: Too large(%lu)\n", length);
        return ZX_ERR_OUT_OF_RANGE;
      }
      request->length = static_cast<uint32_t>(length);
      request->vmo_offset = 0;
      request->dev_offset = offset / block_size;

      if (zx_status_t status = client.Transaction(request, 1); status != ZX_OK) {
        ERROR("Error writing trailing zeroes length:%u dev_offset:%lu vmo_offset:%lu\n",
              request->length, request->dev_offset, request->vmo_offset);
        return status;
      }

      offset += request->length * block_size;
      bytes_left -= request->length * block_size;
    }
  }
  return ZX_OK;
}

}  // namespace

fbl::unique_fd TryBindToFvmDriver(const fbl::unique_fd& devfs_root,
                                  const fbl::unique_fd& partition_fd, zx::duration timeout) {
  char path[PATH_MAX] = {};
  zx_status_t status = GetTopoPathFromFd(partition_fd, path, sizeof(path));
  if (status != ZX_OK) {
    ERROR("Failed to get topological path\n");
    return fbl::unique_fd();
  }

  char fvm_path[PATH_MAX];
  snprintf(fvm_path, sizeof(fvm_path), "%s/fvm", &path[5]);

  fbl::unique_fd fvm(openat(devfs_root.get(), fvm_path, O_RDWR));
  if (fvm) {
    return fvm;
  }

  fdio_cpp::UnownedFdioCaller caller(partition_fd.get());
  constexpr char kFvmDriverLib[] = "fvm.so";
  auto resp = fidl::WireCall(caller.borrow_as<fuchsia_device::Controller>())
                  ->Rebind(fidl::StringView(kFvmDriverLib));
  status = resp.status();
  if (status == ZX_OK) {
    if (resp->is_error()) {
      status = resp->error_value();
    }
  }
  if (status != ZX_OK && status != ZX_ERR_ALREADY_BOUND) {
    ERROR("Could not rebind fvm driver, Error %d\n", status);
    return fbl::unique_fd();
  }

  if (wait_for_device_at(devfs_root.get(), fvm_path, timeout.get()) != ZX_OK) {
    ERROR("Error waiting for fvm driver to bind\n");
    return fbl::unique_fd();
  }
  return fbl::unique_fd(openat(devfs_root.get(), fvm_path, O_RDWR));
}

fbl::unique_fd FvmPartitionFormat(const fbl::unique_fd& devfs_root, fbl::unique_fd partition_fd,
                                  const fvm::SparseImage& header, BindOption option,
                                  FormatResult* format_result) {
  // Although the format (based on the magic in the FVM superblock)
  // indicates this is (or at least was) an FVM image, it may be invalid.
  //
  // Attempt to bind the FVM driver to this partition, but fall-back to
  // reinitializing the FVM image so the rest of the paving
  // process can continue successfully.
  fbl::unique_fd fvm_fd;
  if (format_result != nullptr) {
    *format_result = FormatResult::kUnknown;
  }
  fdio_cpp::UnownedFdioCaller partition_connection(partition_fd.get());
  fidl::UnownedClientEnd partition_device = partition_connection.borrow_as<block::Block>();
  if (option == BindOption::TryBind) {
    fs_management::DiskFormat df = fs_management::DetectDiskFormat(partition_device);
    if (df == fs_management::kDiskFormatFvm) {
      fvm_fd = TryBindToFvmDriver(devfs_root, partition_fd, zx::sec(3));
      if (fvm_fd) {
        LOG("Found already formatted FVM.\n");
        fdio_cpp::UnownedFdioCaller volume_manager(fvm_fd.get());
        auto result = fidl::WireCall(volume_manager.borrow_as<volume::VolumeManager>())->GetInfo();
        if (result.status() == ZX_OK) {
          auto get_maximum_slice_count = [](const fvm::SparseImage& header) {
            return fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions, header.maximum_disk_size,
                                             header.slice_size)
                .GetAllocationTableAllocatedEntryCount();
          };
          if (result.value().info->slice_size != header.slice_size) {
            ERROR("Mismatched slice size. Reinitializing FVM.\n");
          } else if (header.maximum_disk_size > 0 &&
                     result.value().info->maximum_slice_count < get_maximum_slice_count(header)) {
            ERROR("Mismatched maximum slice count. Reinitializing FVM.\n");
          } else {
            if (format_result != nullptr) {
              *format_result = FormatResult::kPreserved;
            }
            return fvm_fd;
          }
        } else {
          ERROR("Could not query FVM for info. Reinitializing FVM.\n");
        }
      } else {
        ERROR(
            "Saw fs_management::kDiskFormatFvm, but could not bind driver. Reinitializing FVM.\n");
      }
    }
  }

  LOG("Initializing partition as FVM\n");
  {
    if (format_result != nullptr) {
      *format_result = FormatResult::kReformatted;
    }

    auto block_info_result = fidl::WireCall(partition_device)->GetInfo();
    if (!block_info_result.ok()) {
      ERROR("Failed to query block info: %s\n", zx_status_get_string(block_info_result.status()));
      return fbl::unique_fd();
    }

    uint64_t initial_disk_size =
        block_info_result.value().info->block_count * block_info_result.value().info->block_size;
    uint64_t max_disk_size =
        (header.maximum_disk_size == 0) ? initial_disk_size : header.maximum_disk_size;

    zx_status_t status =
        fs_management::FvmInitPreallocated(partition_connection.borrow_as<block::Block>(),
                                           initial_disk_size, max_disk_size, header.slice_size);
    if (status != ZX_OK) {
      ERROR("Failed to initialize fvm: %s\n", zx_status_get_string(status));
      return fbl::unique_fd();
    }
  }

  return TryBindToFvmDriver(devfs_root, partition_fd, zx::sec(3));
}

namespace {

// Formats a block device as a zxcrypt volume.
//
// On success, returns a file descriptor to an FVM.
// On failure, returns -1
zx_status_t ZxcryptCreate(PartitionInfo* part) {
  // TODO(security): fxbug.dev/31073. We need to bind with channel in order to pass a key here.
  // TODO(security): fxbug.dev/31733. The created volume must marked as needing key rotation.

  fbl::unique_fd devfs_root(open("/dev", O_RDONLY));

  zxcrypt::VolumeManager zxcrypt_manager(std::move(part->new_part), std::move(devfs_root));
  zx::channel client_chan;
  if (zx_status_t status = zxcrypt_manager.OpenClient(zx::sec(3), client_chan); status != ZX_OK) {
    ERROR("Could not open zxcrypt volume manager\n");
    return status;
  }
  zxcrypt::EncryptedVolumeClient zxcrypt_client(std::move(client_chan));
  uint8_t slot = 0;
  if (zx_status_t status = zxcrypt_client.FormatWithImplicitKey(slot); status != ZX_OK) {
    ERROR("Could not create zxcrypt volume\n");
    return status;
  }

  if (zx_status_t status = zxcrypt_client.UnsealWithImplicitKey(slot); status != ZX_OK) {
    ERROR("Could not unseal zxcrypt volume\n");
    return status;
  }

  if (zx_status_t status = zxcrypt_manager.OpenInnerBlockDevice(zx::sec(3), &part->new_part);
      status != ZX_OK) {
    ERROR("Could not open zxcrypt volume\n");
    return status;
  }

  return ZX_OK;
}

// Returns |ZX_OK| if |partition_fd| is a child of |fvm_fd|.
zx_status_t FvmPartitionIsChild(const fbl::unique_fd& fvm_fd, const fbl::unique_fd& partition_fd) {
  char fvm_path[PATH_MAX];
  char part_path[PATH_MAX];
  if (zx_status_t status = GetTopoPathFromFd(fvm_fd, fvm_path, sizeof(fvm_path)); status != ZX_OK) {
    ERROR("Couldn't get topological path of FVM\n");
    return status;
  }
  if (zx_status_t status = GetTopoPathFromFd(partition_fd, part_path, sizeof(part_path));
      status != ZX_OK) {
    ERROR("Couldn't get topological path of partition\n");
    return status;
  }
  if (strncmp(fvm_path, part_path, strlen(fvm_path)) != 0) {
    ERROR("Partition does not exist within FVM\n");
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

void RecommendWipe(const char* problem) {
  Warn(problem, "Please run 'install-disk-image wipe' to wipe your partitions");
}

// Calculate the amount of space necessary for the incoming partitions,
// validating the header along the way. Additionally, deletes any old partitions
// which match the type GUID of the provided partition.
//
// Parses the information from the |reader| into |parts|.
zx_status_t PreProcessPartitions(const fbl::unique_fd& fvm_fd,
                                 const std::unique_ptr<fvm::SparseReader>& reader,
                                 const fbl::Array<PartitionInfo>& parts,
                                 size_t* out_requested_slices) {
  fvm::PartitionDescriptor* part = reader->Partitions();
  fvm::SparseImage* hdr = reader->Image();

  // Validate the header and determine the necessary slice requirements for
  // all partitions and all offsets.
  size_t requested_slices = 0;
  for (size_t p = 0; p < hdr->partition_count; p++) {
    parts[p].pd = part;
    memcpy(&parts[p].aligned_pd, part, sizeof(fvm::PartitionDescriptor));
    if (parts[p].pd->magic != fvm::kPartitionDescriptorMagic) {
      ERROR("Bad partition magic\n");
      return ZX_ERR_IO;
    }

    zx_status_t status = WipeAllFvmPartitionsWithGuid(fvm_fd, parts[p].pd->type);
    if (status != ZX_OK) {
      ERROR("Failure wiping old partitions matching this GUID\n");
      return status;
    }

    fvm::ExtentDescriptor ext = GetExtent(parts[p].pd, 0);
    if (ext.magic != fvm::kExtentDescriptorMagic) {
      ERROR("Bad extent magic\n");
      return ZX_ERR_IO;
    }
    if (ext.slice_start != 0) {
      ERROR("First slice must start at zero\n");
      return ZX_ERR_IO;
    }
    if (ext.slice_count == 0) {
      ERROR("Extents must have > 0 slices\n");
      return ZX_ERR_IO;
    }
    if (ext.extent_length > ext.slice_count * hdr->slice_size) {
      ERROR("Extent length(%lu) must fit within allocated slice count(%lu * %lu)\n",
            ext.extent_length, ext.slice_count, hdr->slice_size);
      return ZX_ERR_IO;
    }

    // Filter drivers may require additional space.
    if ((parts[p].aligned_pd.flags & fvm::kSparseFlagZxcrypt) != 0) {
      requested_slices += kZxcryptExtraSlices;
    }

    for (size_t e = 1; e < parts[p].aligned_pd.extent_count; e++) {
      ext = GetExtent(parts[p].pd, e);
      if (ext.magic != fvm::kExtentDescriptorMagic) {
        ERROR("Bad extent magic\n");
        return ZX_ERR_IO;
      }
      if (ext.slice_count == 0) {
        ERROR("Extents must have > 0 slices\n");
        return ZX_ERR_IO;
      }
      if (ext.extent_length > ext.slice_count * hdr->slice_size) {
        char name[BLOCK_NAME_LEN + 1];
        name[BLOCK_NAME_LEN] = '\0';
        memcpy(&name, parts[p].aligned_pd.name, BLOCK_NAME_LEN);
        ERROR("Partition(%s) extent length(%lu) must fit within allocated slice count(%lu * %lu)\n",
              name, ext.extent_length, ext.slice_count, hdr->slice_size);
        return ZX_ERR_IO;
      }

      requested_slices += ext.slice_count;
    }
    part = reinterpret_cast<fvm::PartitionDescriptor*>(
        reinterpret_cast<uint8_t*>(parts[p].pd) + sizeof(fvm::PartitionDescriptor) +
        parts[p].aligned_pd.extent_count * sizeof(fvm::ExtentDescriptor));
  }

  *out_requested_slices = requested_slices;
  return ZX_OK;
}

// Allocates the space requested by the partitions by creating new
// partitions and filling them with extents. This guarantees that
// streaming the data to the device will not run into "no space" issues
// later.
zx_status_t AllocatePartitions(const fbl::unique_fd& devfs_root, const fbl::unique_fd& fvm_fd,
                               fbl::Array<PartitionInfo>* parts) {
  for (PartitionInfo& part_info : *parts) {
    fvm::ExtentDescriptor ext = GetExtent(part_info.pd, 0);
    alloc_req_t alloc = {};
    // Allocate this partition as inactive so it gets deleted on the next
    // reboot if this stream fails.
    alloc.flags = part_info.active ? 0 : volume::wire::kAllocatePartitionFlagInactive;
    alloc.slice_count = ext.slice_count;
    memcpy(&alloc.type, part_info.pd->type, sizeof(alloc.type));
    memcpy(&alloc.guid, uuid::Uuid::Generate().bytes(), uuid::kUuidSize);
    memcpy(&alloc.name, part_info.pd->name, sizeof(alloc.name));
    LOG("Allocating partition %s consisting of %zu slices\n", alloc.name, alloc.slice_count);
    if (auto fd_or =
            fs_management::FvmAllocatePartitionWithDevfs(devfs_root.get(), fvm_fd.get(), &alloc);
        fd_or.is_error()) {
      ERROR("Couldn't allocate partition\n");
      return ZX_ERR_NO_SPACE;
    } else {
      part_info.new_part = *std::move(fd_or);
    }

    // Add filter drivers.
    if ((part_info.pd->flags & fvm::kSparseFlagZxcrypt) != 0) {
      LOG("Creating zxcrypt volume\n");
      zx_status_t status = ZxcryptCreate(&part_info);
      if (status != ZX_OK) {
        return status;
      }
    }

    // The 0th index extent is allocated alongside the partition, so we
    // begin indexing from the 1st extent here.
    for (size_t e = 1; e < part_info.pd->extent_count; e++) {
      ext = GetExtent(part_info.pd, e);
      uint64_t offset = ext.slice_start;
      uint64_t length = ext.slice_count;

      fdio_cpp::UnownedFdioCaller partition_connection(part_info.new_part.get());
      auto result =
          fidl::WireCall(partition_connection.borrow_as<volume::Volume>())->Extend(offset, length);
      auto status = result.ok() ? result.value().status : result.status();
      if (status != ZX_OK) {
        ERROR("Failed to extend partition: %s\n", zx_status_get_string(status));
        return status;
      }
    }
  }

  return ZX_OK;
}

// Holds the description of a partition with a single extent. Note that even though some code asks
// for a PartitionDescriptor, in reality it treats that as a descriptor followed by a bunch of
// extents, so this copes with that de-facto pattern.
struct FvmPartition {
  // Returns an FVM partition with no real information about extents.  In order to
  // use the partitions, they should be formatted with the appropriate filesystem.
  static FvmPartition Make(const std::array<uint8_t, fvm::kGuidSize> partition_type,
                           std::string_view name) {
    FvmPartition partition{.extent = {
                               .slice_count = 1,
                           }};
    std::copy(std::begin(partition_type), std::end(partition_type),
              std::begin(partition.descriptor.type));
    std::copy(name.begin(), name.end(), std::begin(partition.descriptor.name));
    return partition;
  }

  fvm::PartitionDescriptor descriptor;
  fvm::ExtentDescriptor extent;
};

}  // namespace

// Deletes all partitions within the FVM with a type GUID matching |type_guid|
// until there are none left.
zx_status_t WipeAllFvmPartitionsWithGuid(const fbl::unique_fd& fvm_fd, const uint8_t type_guid[]) {
  char fvm_topo_path[PATH_MAX] = {0};
  fbl::unique_fd old_part;
  if (zx_status_t status = GetTopoPathFromFd(fvm_fd, fvm_topo_path, sizeof(fvm_topo_path));
      status != ZX_OK) {
    ERROR("Couldn't get topological path of FVM!\n");
    return status;
  }

  fs_management::PartitionMatcher matcher{
      .type_guid = type_guid,
      .parent_device = fvm_topo_path,
  };
  for (;;) {
    std::string name;
    auto old_part_or = fs_management::OpenPartition(matcher, ZX_MSEC(500), &name);
    if (old_part_or.is_error())
      break;
    old_part = *std::move(old_part_or);
    bool is_vpartition;
    if (FvmIsVirtualPartition(old_part, &is_vpartition) != ZX_OK) {
      ERROR("Couldn't confirm old vpartition type\n");
      return ZX_ERR_IO;
    }
    if (FvmPartitionIsChild(fvm_fd, old_part) != ZX_OK) {
      RecommendWipe("Streaming a partition type which also exists outside the target FVM");
      return ZX_ERR_BAD_STATE;
    }
    if (!is_vpartition) {
      RecommendWipe("Streaming a partition type which also exists in a GPT");
      return ZX_ERR_BAD_STATE;
    }

    // We're paving a partition that already exists within the FVM: let's
    // destroy it before we pave anew.

    fdio_cpp::UnownedFdioCaller partition_connection(old_part.get());
    auto result = fidl::WireCall(partition_connection.borrow_as<volume::Volume>())->Destroy();
    zx_status_t status = result.ok() ? result.value().status : result.status();
    if (status != ZX_OK) {
      ERROR("Couldn't destroy partition: %s\n", zx_status_get_string(status));
      return status;
    }
  }

  return ZX_OK;
}

zx::result<> AllocateEmptyPartitions(const fbl::unique_fd& devfs_root,
                                     const fbl::unique_fd& fvm_fd) {
  FvmPartition fvm_partitions[] = {
      FvmPartition::Make(std::array<uint8_t, fvm::kGuidSize>(GUID_BLOB_VALUE),
                         fshost::kBlobfsPartitionLabel),
      FvmPartition::Make(std::array<uint8_t, fvm::kGuidSize>(GUID_DATA_VALUE),
                         fshost::kDataPartitionLabel)};
  fbl::Array<PartitionInfo> partitions(new PartitionInfo[2]{{
                                                                .pd = &fvm_partitions[0].descriptor,
                                                                .active = true,
                                                            },
                                                            {
                                                                .pd = &fvm_partitions[1].descriptor,
                                                                .active = true,
                                                            }},
                                       2);
  return zx::make_result(AllocatePartitions(devfs_root, fvm_fd, &partitions));
}

zx::result<> FvmStreamPartitions(const fbl::unique_fd& devfs_root,
                                 std::unique_ptr<PartitionClient> partition_client,
                                 std::unique_ptr<fvm::ReaderInterface> payload) {
  std::unique_ptr<fvm::SparseReader> reader;
  zx::result<> status = zx::ok();
  if (status = zx::make_result(fvm::SparseReader::Create(std::move(payload), &reader));
      status.is_error()) {
    return status.take_error();
  }

  LOG("Header Validated - OK\n");

  fvm::SparseImage* hdr = reader->Image();
  // Acquire an fd to the FVM, either by finding one that already
  // exists, or formatting a new one.
  fbl::unique_fd fvm_fd(
      FvmPartitionFormat(devfs_root, partition_client->block_fd(), *hdr, BindOption::TryBind));
  if (!fvm_fd) {
    ERROR("Couldn't find FVM partition\n");
    return zx::error(ZX_ERR_IO);
  }

  fbl::Array<PartitionInfo> parts(new PartitionInfo[hdr->partition_count], hdr->partition_count);

  // Parse the incoming image and calculate its size.
  //
  // Additionally, delete the old versions of any new partitions.
  size_t requested_slices = 0;
  if (status = zx::make_result(PreProcessPartitions(fvm_fd, reader, parts, &requested_slices));
      status.is_error()) {
    ERROR("Failed to validate partitions: %s\n", status.status_string());
    return status.take_error();
  }

  // Contend with issues from an image that may be too large for this device.
  VolumeManagerInfo info;
  if (auto info_or = fs_management::FvmQuery(fvm_fd.get()); info_or.is_error()) {
    ERROR("Failed to acquire FVM info: %s\n", status.status_string());
    return info_or.take_error();
  } else {
    info = reinterpret_cast<VolumeManagerInfo&>(*info_or);
  }
  size_t free_slices = info.slice_count - info.assigned_slice_count;
  if (info.slice_count < requested_slices) {
    char buf[256];
    snprintf(buf, sizeof(buf), "Image size (%zu) > Storage size (%zu)",
             requested_slices * hdr->slice_size, info.slice_count * hdr->slice_size);
    Warn(buf, "Image is too large to be paved to device");
    return zx::error(ZX_ERR_NO_SPACE);
  }
  if (free_slices < requested_slices) {
    Warn("Not enough space to non-destructively pave",
         "Automatically reinitializing FVM; Expect data loss");
    fvm_fd =
        FvmPartitionFormat(devfs_root, partition_client->block_fd(), *hdr, BindOption::Reformat);
    if (!fvm_fd) {
      ERROR("Couldn't reformat FVM partition.\n");
      return zx::error(ZX_ERR_IO);
    }
    LOG("FVM Reformatted successfully.\n");
  }

  LOG("Partitions pre-validated successfully: Enough space exists to pave.\n");

  // Actually allocate the storage for the incoming image.
  if (status = zx::make_result(AllocatePartitions(devfs_root, fvm_fd, &parts)); status.is_error()) {
    ERROR("Failed to allocate partitions: %s\n", status.status_string());
    return status.take_error();
  }

  LOG("Partition space pre-allocated successfully.\n");

  constexpr size_t vmo_size = 1 << 20;

  fzl::VmoMapper mapping;
  zx::vmo vmo;
  if (mapping.CreateAndMap(vmo_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo) != ZX_OK) {
    ERROR("Failed to create stream VMO\n");
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  fdio_cpp::FdioCaller volume_manager(std::move(fvm_fd));

  // Now that all partitions are preallocated, begin streaming data to them.
  for (size_t p = 0; p < parts.size(); p++) {
    vmoid_t vmoid;
    std::unique_ptr<block_client::Client> client;
    auto status = zx::make_result(RegisterFastBlockIo(parts[p].new_part, vmo, &vmoid, &client));
    if (status.is_error()) {
      ERROR("Failed to register fast block IO\n");
      return status.take_error();
    }

    fdio_cpp::UnownedFdioCaller partition_connection(parts[p].new_part.get());
    auto result = fidl::WireCall(partition_connection.borrow_as<block::Block>())->GetInfo();
    if (!result.ok()) {
      ERROR("Couldn't get partition block info: %s\n", zx_status_get_string(result.status()));
      return zx::error(result.status());
    }
    const auto& response = result.value();
    if (response.status != ZX_OK) {
      ERROR("Couldn't get partition block info: %s\n", zx_status_get_string(response.status));
      return zx::error(response.status);
    }

    size_t block_size = response.info->block_size;

    block_fifo_request_t request;
    request.group = 0;
    request.vmoid = vmoid;
    request.opcode = BLOCKIO_WRITE;

    LOG("Streaming partition %zu\n", p);
    status = zx::make_result(
        StreamFvmPartition(reader.get(), &parts[p], mapping, *client, block_size, &request));
    LOG("Done streaming partition %zu\n", p);
    if (status.is_error()) {
      ERROR("Failed to stream partition status=%d\n", status.error_value());
      return status.take_error();
    }
    if (status = zx::make_result(FlushClient(client.get())); status.is_error()) {
      ERROR("Failed to flush client\n");
      return status.take_error();
    }
    LOG("Done flushing partition %zu\n", p);
  }

  for (const PartitionInfo& part_info : parts) {
    fdio_cpp::UnownedFdioCaller partition_connection(part_info.new_part.get());
    // Upgrade the old partition (currently active) to the new partition (currently
    // inactive) so the new partition persists.
    auto result =
        fidl::WireCall(partition_connection.borrow_as<partition::Partition>())->GetInstanceGuid();
    if (!result.ok() || result.value().status != ZX_OK) {
      ERROR("Failed to get unique GUID of new partition\n");
      return zx::error(ZX_ERR_BAD_STATE);
    }
    auto* guid = result.value().guid.get();

    auto result2 =
        fidl::WireCall(volume_manager.borrow_as<volume::VolumeManager>())->Activate(*guid, *guid);
    if (result2.status() != ZX_OK || result2.value().status != ZX_OK) {
      ERROR("Failed to upgrade partition\n");
      return zx::error(ZX_ERR_IO);
    }
  }

  return zx::ok();
}

// Unbinds the FVM driver from the given device. Assumes that the driver is either
// loaded or not (but not in the process of being loaded).
zx_status_t FvmUnbind(const fbl::unique_fd& devfs_root, const char* device) {
  size_t len = strnlen(device, PATH_MAX);
  constexpr const char* kDevPath = "/dev/";
  constexpr size_t kDevPathLen = std::char_traits<char>::length(kDevPath);

  if (len == PATH_MAX || len <= kDevPathLen) {
    ERROR("Invalid device name: %s\n", device);
    return ZX_ERR_INVALID_ARGS;
  }
  fbl::StringBuffer<PATH_MAX> name_buffer;
  name_buffer.Append(device + kDevPathLen);
  name_buffer.Append("/fvm");

  fdio_cpp::UnownedFdioCaller caller(devfs_root.get());
  zx::result channel = component::ConnectAt<device::Controller>(caller.directory(), name_buffer);
  if (channel.is_error()) {
    ERROR("Unable to connect to FVM service: %s on device %s\n", channel.status_string(),
          name_buffer.c_str());
    return channel.status_value();
  }
  auto resp = fidl::WireCall(channel.value())->ScheduleUnbind();
  if (resp.status() != ZX_OK) {
    ERROR("Failed to schedule FVM unbind: %s on device %s\n", zx_status_get_string(resp.status()),
          name_buffer.data());
    return resp.status();
  }
  if (resp->is_error()) {
    ERROR("FVM unbind failed: %s on device %s\n", zx_status_get_string(resp->error_value()),
          name_buffer.data());
    return resp->error_value();
  }
  return ZX_OK;
}

}  // namespace paver
