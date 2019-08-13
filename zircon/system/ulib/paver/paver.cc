// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "paver.h"
#undef GUID_LEN

#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <block-client/cpp/client.h>
#include <crypto/bytes.h>
#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/volume/llcpp/fidl.h>
#include <fuchsia/hardware/skipblock/llcpp/fidl.h>
#include <fuchsia/hardware/zxcrypt/llcpp/fidl.h>
#include <fvm/format.h>
#include <fvm/fvm-sparse.h>
#include <fvm/sparse-reader.h>
#include <lib/cksum.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fzl/fdio.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/fifo.h>
#include <lib/zx/vmo.h>
#include <ramdevice-client/ramdisk.h>
#include <zircon/boot/image.h>
#include <zircon/device/block.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zxcrypt/fdio-volume.h>

#include <utility>

#include "pave-logging.h"
#include "pave-utils.h"
#include "stream-reader.h"
#include "vmo-reader.h"

#define ZXCRYPT_DRIVER_LIB "/boot/driver/zxcrypt.so"

namespace paver {
namespace {

namespace block = ::llcpp::fuchsia::hardware::block;
namespace partition = ::llcpp::fuchsia::hardware::block::partition;
namespace volume = ::llcpp::fuchsia::hardware::block::volume;
namespace skipblock = ::llcpp::fuchsia::hardware::skipblock;

using ::llcpp::fuchsia::hardware::block::volume::VolumeInfo;

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
    fzl::UnownedFdioCaller caller(fd.get());
    auto result = ::llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(caller.channel());
    if (!result.ok()) {
        return result.status();
    }
    const auto& response = result.value();
    if (response.status != ZX_OK) {
        return response.status;
    }
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
  PartitionInfo() : pd(nullptr) {}

  fvm::partition_descriptor_t* pd;
  fbl::unique_fd new_part;
};

inline fvm::extent_descriptor_t* GetExtent(fvm::partition_descriptor_t* pd, size_t extent) {
  return reinterpret_cast<fvm::extent_descriptor_t*>(reinterpret_cast<uintptr_t>(pd) +
                                                     sizeof(fvm::partition_descriptor_t) +
                                                     extent * sizeof(fvm::extent_descriptor_t));
}

// Registers a FIFO
zx_status_t RegisterFastBlockIo(const fbl::unique_fd& fd, const zx::vmo& vmo, vmoid_t* out_vmoid,
                                block_client::Client* out_client) {
    fzl::UnownedFdioCaller caller(fd.get());

    auto result = block::Block::Call::GetFifo(caller.channel());
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

    auto result2 = block::Block::Call::AttachVmo(caller.channel(), std::move(dup));
    if (result2.status() != ZX_OK) {
        return result2.status();
    }
    const auto& response2 = result2.value();
    if (response2.status != ZX_OK) {
        return response2.status;
    }

    *out_vmoid = response2.vmoid->id;
    return block_client::Client::Create(std::move(response.fifo), out_client);
}

// Stream an FVM partition to disk.
zx_status_t StreamFvmPartition(fvm::SparseReader* reader, PartitionInfo* part,
                               const fzl::VmoMapper& mapper, const block_client::Client& client,
                               size_t block_size, block_fifo_request_t* request) {
  size_t slice_size = reader->Image()->slice_size;
  const size_t vmo_cap = mapper.size();
  for (size_t e = 0; e < part->pd->extent_count; e++) {
    LOG("Writing extent %zu... \n", e);
    fvm::extent_descriptor_t* ext = GetExtent(part->pd, e);
    size_t offset = ext->slice_start * slice_size;
    size_t bytes_left = ext->extent_length;

    // Write real data
    while (bytes_left > 0) {
      size_t vmo_sz = 0;
      size_t actual;
      zx_status_t status = reader->ReadData(&reinterpret_cast<uint8_t*>(mapper.start())[vmo_sz],
                                            fbl::min(bytes_left, vmo_cap - vmo_sz), &actual);
      vmo_sz += actual;
      bytes_left -= actual;

      if (vmo_sz == 0) {
        ERROR("Read nothing from src_fd; %zu bytes left\n", bytes_left);
        return ZX_ERR_IO;
      } else if (vmo_sz % block_size != 0) {
        ERROR("Cannot write non-block size multiple: %zu\n", vmo_sz);
        return ZX_ERR_IO;
      } else if (status != ZX_OK) {
        ERROR("Error reading partition data\n");
        return status;
      }

      uint64_t length = vmo_sz / block_size;
      if (length > UINT32_MAX) {
        ERROR("Error writing partition: Too large\n");
        return ZX_ERR_OUT_OF_RANGE;
      }
      request->length = static_cast<uint32_t>(length);
      request->vmo_offset = 0;
      request->dev_offset = offset / block_size;

      ssize_t r;
      if ((r = client.Transaction(request, 1)) != ZX_OK) {
        ERROR("Error writing partition data\n");
        return static_cast<zx_status_t>(r);
      }

      offset += vmo_sz;
    }

    // Write trailing zeroes (which are implied, but were omitted from
    // transfer).
    bytes_left = (ext->slice_count * slice_size) - ext->extent_length;
    if (bytes_left > 0) {
      LOG("%zu bytes written, %zu zeroes left\n", ext->extent_length, bytes_left);
      memset(mapper.start(), 0, vmo_cap);
    }
    while (bytes_left > 0) {
      uint64_t length = fbl::min(bytes_left, vmo_cap) / block_size;
      if (length > UINT32_MAX) {
        ERROR("Error writing trailing zeroes: Too large\n");
        return ZX_ERR_OUT_OF_RANGE;
      }
      request->length = static_cast<uint32_t>(length);
      request->vmo_offset = 0;
      request->dev_offset = offset / block_size;

      zx_status_t status;
      if ((status = client.Transaction(request, 1)) != ZX_OK) {
        ERROR("Error writing trailing zeroes\n");
        return status;
      }

      offset += request->length * block_size;
      bytes_left -= request->length * block_size;
    }
  }
  return ZX_OK;
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

// Attempt to bind an FVM driver to a partition fd.
fbl::unique_fd TryBindToFvmDriver(const fbl::unique_fd& devfs_root,
                                  const fbl::unique_fd& partition_fd, zx::duration timeout) {
    char path[PATH_MAX] = {};
    zx_status_t status = GetTopoPathFromFd(partition_fd, path, sizeof(path));
    if (status != ZX_OK) {
        ERROR("Failed to get topological path\n");
        return fbl::unique_fd();
    }

    // We assume the FVM will either have completed binding, or is not bound at all. This is ensured
    // by the paver always waiting for the FVM to bind after invoking ControllerBind.
    char fvm_path[PATH_MAX];
    snprintf(fvm_path, sizeof(fvm_path), "%s/fvm", &path[5]);

    fbl::unique_fd fvm(openat(devfs_root.get(), fvm_path, O_RDWR));
    if (fvm) {
        return fvm;
    }

    fzl::UnownedFdioCaller caller(partition_fd.get());
    constexpr char kFvmDriverLib[] = "/boot/driver/fvm.so";
    auto result = ::llcpp::fuchsia::device::Controller::Call::Bind(
        caller.channel(), fidl::StringView(strlen(kFvmDriverLib), kFvmDriverLib));
    status = result.ok() ? result.value().status : result.status();
    if (status != ZX_OK) {
        ERROR("Could not bind fvm driver\n");
        return fbl::unique_fd();
    }

    if (wait_for_device_at(devfs_root.get(), fvm_path, timeout.get()) != ZX_OK) {
        ERROR("Error waiting for fvm driver to bind\n");
        return fbl::unique_fd();
    }
    return fbl::unique_fd(openat(devfs_root.get(), fvm_path, O_RDWR));
}

}  // namespace

// Formats the FVM within the provided partition if it is not already formatted.
//
// On success, returns a file descriptor to an FVM.
// On failure, returns -1
fbl::unique_fd FvmPartitionFormat(const fbl::unique_fd& devfs_root, fbl::unique_fd partition_fd,
                                  size_t slice_size, BindOption option) {
  // Although the format (based on the magic in the FVM superblock)
  // indicates this is (or at least was) an FVM image, it may be invalid.
  //
  // Attempt to bind the FVM driver to this partition, but fall-back to
  // reinitializing the FVM image so the rest of the paving
  // process can continue successfully.
  fbl::unique_fd fvm_fd;
  if (option == BindOption::TryBind) {
    disk_format_t df = detect_disk_format(partition_fd.get());
    if (df == DISK_FORMAT_FVM) {
      fvm_fd = TryBindToFvmDriver(devfs_root, partition_fd, zx::sec(3));
      if (fvm_fd) {
        LOG("Found already formatted FVM.\n");
        VolumeInfo info;
        zx_status_t status = fvm_query(
            fvm_fd.get(), reinterpret_cast<fuchsia_hardware_block_volume_VolumeInfo*>(&info));
        if (status == ZX_OK) {
          if (info.slice_size == slice_size) {
            return fvm_fd;
          } else {
            ERROR("Mismatched slice size. Reinitializing FVM.\n");
          }
        } else {
          ERROR("Could not query FVM for info. Reinitializing FVM.\n");
        }
      } else {
        ERROR("Saw DISK_FORMAT_FVM, but could not bind driver. Reinitializing FVM.\n");
      }
    }
  }

  LOG("Initializing partition as FVM\n");
  zx_status_t status = fvm_init(partition_fd.get(), slice_size);
  if (status != ZX_OK) {
    ERROR("Failed to initialize fvm: %s\n", zx_status_get_string(status));
    return fbl::unique_fd();
  }

    {
        fzl::UnownedFdioCaller partition_connection(partition_fd.get());
        auto result = block::Block::Call::RebindDevice(partition_connection.channel());
        status = result.ok() ? result.value().status : result.status();
        if (status != ZX_OK) {
            ERROR("Could not rebind partition: %s\n", zx_status_get_string(status));
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
  zx_status_t status;

  char path[PATH_MAX];
  status = GetTopoPathFromFd(part->new_part, path, sizeof(path));
  if (status != ZX_OK) {
    ERROR("Failed to get topological path\n");
    return status;
  }
  // TODO(security): ZX-1130. We need to bind with channel in order to pass a key here.
  // TODO(security): ZX-1864. The created volume must marked as needing key rotation.

  fbl::unique_fd devfs_root(open("/dev", O_RDONLY));
  fbl::unique_ptr<zxcrypt::FdioVolume> volume;
  if ((status = zxcrypt::FdioVolume::CreateWithDeviceKey(
           std::move(part->new_part), std::move(devfs_root), &volume)) != ZX_OK) {
    ERROR("Could not create zxcrypt volume\n");
    return status;
  }
  zx::channel zxcrypt_manager_chan;
  if ((status = volume->OpenManager(zx::sec(3), zxcrypt_manager_chan.reset_and_get_address())) !=
      ZX_OK) {
    ERROR("Could not open zxcrypt volume manager\n");
    return status;
  }

  zxcrypt::FdioVolumeManager zxcrypt_manager(std::move(zxcrypt_manager_chan));
  uint8_t slot = 0;
  if ((status = zxcrypt_manager.UnsealWithDeviceKey(slot)) != ZX_OK) {
    ERROR("Could not unseal zxcrypt volume\n");
    return status;
  }

  if ((status = volume->Open(zx::sec(3), &part->new_part)) != ZX_OK) {
    ERROR("Could not open zxcrypt volume\n");
    return status;
  }

  fvm::extent_descriptor_t* ext = GetExtent(part->pd, 0);
  size_t reserved = volume->reserved_slices();

  // |Create| guarantees at least |reserved| + 1 slices are allocated.  If the first extent had a
  // single slice, we're done.
  size_t allocated = fbl::max(reserved + 1, ext->slice_count);
  size_t needed = reserved + ext->slice_count;
  if (allocated >= needed) {
    return ZX_OK;
  }

    // Otherwise, extend by the number of slices we stole for metadata
    uint64_t offset = allocated - reserved;
    uint64_t length = needed - allocated;
    {
        fzl::UnownedFdioCaller partition_connection(part->new_part.get());
        auto result = volume::Volume::Call::Extend(partition_connection.channel(), offset, length);
        status = result.ok() ? result.value().status : result.status();
        if (status != ZX_OK) {
            ERROR("Failed to extend zxcrypt volume: %s\n", zx_status_get_string(status));
            return status;
        }
    }

  return ZX_OK;
}

// Returns |ZX_OK| if |partition_fd| is a child of |fvm_fd|.
zx_status_t FvmPartitionIsChild(const fbl::unique_fd& fvm_fd, const fbl::unique_fd& partition_fd) {
  char fvm_path[PATH_MAX];
  char part_path[PATH_MAX];
  zx_status_t status;
  if ((status = GetTopoPathFromFd(fvm_fd, fvm_path, sizeof(fvm_path))) != ZX_OK) {
    ERROR("Couldn't get topological path of FVM\n");
    return status;
  } else if ((status = GetTopoPathFromFd(partition_fd, part_path, sizeof(part_path))) != ZX_OK) {
    ERROR("Couldn't get topological path of partition\n");
    return status;
  }
  if (strncmp(fvm_path, part_path, strlen(fvm_path))) {
    ERROR("Partition does not exist within FVM\n");
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

// Warn users about issues in a way that is intended to stand out from
// typical error logs. These errors typically require user intervention,
// or may result in data loss.
void Warn(const char* problem, const char* action) {
  ERROR("-----------------------------------------------------\n");
  ERROR("\n");
  ERROR("%s:\n", problem);
  ERROR("%s\n", action);
  ERROR("\n");
  ERROR("-----------------------------------------------------\n");
}

void RecommendWipe(const char* problem) {
  Warn(problem, "Please run 'install-disk-image wipe' to wipe your partitions");
}

// Deletes all partitions within the FVM with a type GUID matching |type_guid|
// until there are none left.
zx_status_t WipeAllFvmPartitionsWithGUID(const fbl::unique_fd& fvm_fd, const uint8_t type_guid[]) {
  fbl::unique_fd old_part;
  while ((old_part.reset(open_partition(nullptr, type_guid, ZX_MSEC(500), nullptr))), old_part) {
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

        fzl::UnownedFdioCaller partition_connection(old_part.get());
        auto result = volume::Volume::Call::Destroy(partition_connection.channel());
        zx_status_t status = result.ok() ? result.value().status : result.status();
        if (status != ZX_OK) {
            ERROR("Couldn't destroy partition: %s\n", zx_status_get_string(status));
            return status;
        }
    }

  return ZX_OK;
}

// Calculate the amount of space necessary for the incoming partitions,
// validating the header along the way. Additionally, deletes any old partitions
// which match the type GUID of the provided partition.
//
// Parses the information from the |reader| into |parts|.
zx_status_t PreProcessPartitions(const fbl::unique_fd& fvm_fd,
                                 const fbl::unique_ptr<fvm::SparseReader>& reader,
                                 const fbl::Array<PartitionInfo>& parts,
                                 size_t* out_requested_slices) {
  fvm::partition_descriptor_t* part = reader->Partitions();
  fvm::sparse_image_t* hdr = reader->Image();

  // Validate the header and determine the necessary slice requirements for
  // all partitions and all offsets.
  size_t requested_slices = 0;
  for (size_t p = 0; p < hdr->partition_count; p++) {
    parts[p].pd = part;
    if (parts[p].pd->magic != fvm::kPartitionDescriptorMagic) {
      ERROR("Bad partition magic\n");
      return ZX_ERR_IO;
    }

    zx_status_t status = WipeAllFvmPartitionsWithGUID(fvm_fd, parts[p].pd->type);
    if (status != ZX_OK) {
      ERROR("Failure wiping old partitions matching this GUID\n");
      return status;
    }

    fvm::extent_descriptor_t* ext = GetExtent(parts[p].pd, 0);
    if (ext->magic != fvm::kExtentDescriptorMagic) {
      ERROR("Bad extent magic\n");
      return ZX_ERR_IO;
    }
    if (ext->slice_start != 0) {
      ERROR("First slice must start at zero\n");
      return ZX_ERR_IO;
    }
    if (ext->slice_count == 0) {
      ERROR("Extents must have > 0 slices\n");
      return ZX_ERR_IO;
    }
    if (ext->extent_length > ext->slice_count * hdr->slice_size) {
      ERROR("Extent length must fit within allocated slice count\n");
      return ZX_ERR_IO;
    }

    // Filter drivers may require additional space.
    if ((parts[p].pd->flags & fvm::kSparseFlagZxcrypt) != 0) {
      requested_slices += kZxcryptExtraSlices;
    }

    for (size_t e = 1; e < parts[p].pd->extent_count; e++) {
      ext = GetExtent(parts[p].pd, e);
      if (ext->magic != fvm::kExtentDescriptorMagic) {
        ERROR("Bad extent magic\n");
        return ZX_ERR_IO;
      } else if (ext->slice_count == 0) {
        ERROR("Extents must have > 0 slices\n");
        return ZX_ERR_IO;
      } else if (ext->extent_length > ext->slice_count * hdr->slice_size) {
        ERROR("Extent must fit within allocated slice count\n");
        return ZX_ERR_IO;
      }

      requested_slices += ext->slice_count;
    }
    part = reinterpret_cast<fvm::partition_descriptor*>(reinterpret_cast<uintptr_t>(ext) +
                                                        sizeof(fvm::extent_descriptor_t));
  }

  *out_requested_slices = requested_slices;
  return ZX_OK;
}

// Allocates the space requested by the partitions by creating new
// partitions and filling them with extents. This guarantees that
// streaming the data to the device will not run into "no space" issues
// later.
zx_status_t AllocatePartitions(const fbl::unique_fd& fvm_fd,
                               const fbl::Array<PartitionInfo>& parts) {
    for (size_t p = 0; p < parts.size(); p++) {
        fvm::extent_descriptor_t* ext = GetExtent(parts[p].pd, 0);
        alloc_req_t alloc;
        // Allocate this partition as inactive so it gets deleted on the next
        // reboot if this stream fails.
        alloc.flags = volume::AllocatePartitionFlagInactive;
        alloc.slice_count = ext->slice_count;
        memcpy(&alloc.type, parts[p].pd->type, sizeof(alloc.type));
        zx_cprng_draw(alloc.guid, GPT_GUID_LEN);
        memcpy(&alloc.name, parts[p].pd->name, sizeof(alloc.name));
        LOG("Allocating partition %s consisting of %zu slices\n", alloc.name, alloc.slice_count);
        parts[p].new_part.reset(fvm_allocate_partition(fvm_fd.get(), &alloc));
        if (!parts[p].new_part) {
            ERROR("Couldn't allocate partition\n");
            return ZX_ERR_NO_SPACE;
        }

        // Add filter drivers.
        if ((parts[p].pd->flags & fvm::kSparseFlagZxcrypt) != 0) {
            LOG("Creating zxcrypt volume\n");
            zx_status_t status = ZxcryptCreate(&parts[p]);
            if (status != ZX_OK) {
                return status;
            }
        }

        // The 0th index extent is allocated alongside the partition, so we
        // begin indexing from the 1st extent here.
        for (size_t e = 1; e < parts[p].pd->extent_count; e++) {
            ext = GetExtent(parts[p].pd, e);
            uint64_t offset = ext->slice_start;
            uint64_t length = ext->slice_count;

            fzl::UnownedFdioCaller partition_connection(parts[p].new_part.get());
            auto result = volume::Volume::Call::Extend(
                partition_connection.channel(), offset, length);
            auto status = result.ok() ? result.value().status : result.status();
            if (status != ZX_OK) {
                ERROR("Failed to extend partition: %s\n", zx_status_get_string(status));
                return status;
            }
        }
    }

  return ZX_OK;
}

// Given an fd representing a "sparse FVM format", fill the FVM with the
// provided partitions described by |src_fd|.
//
// Decides to overwrite or create new partitions based on the type
// GUID, not the instance GUID.
zx_status_t FvmStreamPartitions(fbl::unique_fd partition_fd,
                                std::unique_ptr<fvm::ReaderInterface> payload) {
  fbl::unique_ptr<fvm::SparseReader> reader;
  zx_status_t status;
  if ((status = fvm::SparseReader::Create(std::move(payload), &reader)) != ZX_OK) {
    return status;
  }

  LOG("Header Validated - OK\n");
  // Duplicate the partition fd; we may need it later if we reformat the FVM.
  fbl::unique_fd partition_fd2(dup(partition_fd.get()));
  if (!partition_fd2) {
    ERROR("Coudln't dup partition fd\n");
    return ZX_ERR_IO;
  }

  fbl::unique_fd devfs_root(open("/dev", O_RDWR));
  if (!devfs_root) {
    ERROR("Couldn't open devfs root\n");
    return ZX_ERR_IO;
  }

  fvm::sparse_image_t* hdr = reader->Image();
  // Acquire an fd to the FVM, either by finding one that already
  // exists, or formatting a new one.
  fbl::unique_fd fvm_fd(FvmPartitionFormat(devfs_root, std::move(partition_fd2), hdr->slice_size,
                                           BindOption::TryBind));
  if (!fvm_fd) {
    ERROR("Couldn't find FVM partition\n");
    return ZX_ERR_IO;
  }

  fbl::Array<PartitionInfo> parts(new PartitionInfo[hdr->partition_count], hdr->partition_count);

  // Parse the incoming image and calculate its size.
  //
  // Additionally, delete the old versions of any new partitions.
  size_t requested_slices = 0;
  if ((status = PreProcessPartitions(fvm_fd, reader, parts, &requested_slices)) != ZX_OK) {
    ERROR("Failed to validate partitions: %s\n", zx_status_get_string(status));
    return status;
  }

  // Contend with issues from an image that may be too large for this device.
  VolumeInfo info;
  status =
      fvm_query(fvm_fd.get(), reinterpret_cast<fuchsia_hardware_block_volume_VolumeInfo*>(&info));
  if (status != ZX_OK) {
    ERROR("Failed to acquire FVM info: %s\n", zx_status_get_string(status));
    return status;
  }
  size_t free_slices = info.pslice_total_count - info.pslice_allocated_count;
  if (info.pslice_total_count < requested_slices) {
    char buf[256];
    snprintf(buf, sizeof(buf), "Image size (%zu) > Storage size (%zu)",
             requested_slices * hdr->slice_size, info.pslice_total_count * hdr->slice_size);
    Warn(buf, "Image is too large to be paved to device");
    return ZX_ERR_NO_SPACE;
  }
  if (free_slices < requested_slices) {
    Warn("Not enough space to non-destructively pave",
         "Automatically reinitializing FVM; Expect data loss");
    fvm_fd = FvmPartitionFormat(devfs_root, std::move(partition_fd), hdr->slice_size,
                                BindOption::Reformat);
    if (!fvm_fd) {
      ERROR("Couldn't reformat FVM partition.\n");
      return ZX_ERR_IO;
    }
    LOG("FVM Reformatted successfully.\n");
  }

  LOG("Partitions pre-validated successfully: Enough space exists to pave.\n");

  // Actually allocate the storage for the incoming image.
  if ((status = AllocatePartitions(fvm_fd, parts)) != ZX_OK) {
    ERROR("Failed to allocate partitions: %s\n", zx_status_get_string(status));
    return status;
  }

  LOG("Partition space pre-allocated successfully.\n");

  constexpr size_t vmo_size = 1 << 20;

  fzl::VmoMapper mapping;
  zx::vmo vmo;
  if ((status = mapping.CreateAndMap(vmo_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
                                     &vmo)) != ZX_OK) {
    ERROR("Failed to create stream VMO\n");
    return ZX_ERR_NO_MEMORY;
  }

  fzl::FdioCaller volume_manager(std::move(fvm_fd));

    // Now that all partitions are preallocated, begin streaming data to them.
    for (size_t p = 0; p < parts.size(); p++) {
        vmoid_t vmoid;
        block_client::Client client;
        zx_status_t status = RegisterFastBlockIo(parts[p].new_part, vmo, &vmoid, &client);
        if (status != ZX_OK) {
            ERROR("Failed to register fast block IO\n");
            return status;
        }

        fzl::UnownedFdioCaller partition_connection(parts[p].new_part.get());
        auto result = block::Block::Call::GetInfo(partition_connection.channel());
        if (!result.ok()) {
            ERROR("Couldn't get partition block info: %s\n", zx_status_get_string(result.status()));
            return result.status();
        }
        const auto& response = result.value();
        if (response.status != ZX_OK) {
            ERROR("Couldn't get partition block info: %s\n",
                  zx_status_get_string(response.status));
            return response.status;
        }

        size_t block_size = response.info->block_size;

        block_fifo_request_t request;
        request.group = 0;
        request.vmoid = vmoid;
        request.opcode = BLOCKIO_WRITE;

    LOG("Streaming partition %zu\n", p);
    status = StreamFvmPartition(reader.get(), &parts[p], mapping, client, block_size, &request);
    LOG("Done streaming partition %zu\n", p);
    if (status != ZX_OK) {
      ERROR("Failed to stream partition status=%d\n", status);
      return status;
    }
    if ((status = FlushClient(client)) != ZX_OK) {
      ERROR("Failed to flush client\n");
      return status;
    }
    LOG("Done flushing partition %zu\n", p);
  }

    for (size_t p = 0; p < parts.size(); p++) {
        fzl::UnownedFdioCaller partition_connection(parts[p].new_part.get());
        // Upgrade the old partition (currently active) to the new partition (currently
        // inactive) so the new partition persists.
        auto result = partition::Partition::Call::GetInstanceGuid(partition_connection.channel());
        if (!result.ok() || result.value().status != ZX_OK) {
            ERROR("Failed to get unique GUID of new partition\n");
            return ZX_ERR_BAD_STATE;
        }
        auto* guid = result.value().guid;

        auto result2 = volume::VolumeManager::Call::Activate(volume_manager.channel(), *guid,
                                                             *guid);
        if (result2.status() != ZX_OK || result2.value().status != ZX_OK) {
            ERROR("Failed to upgrade partition\n");
            return ZX_ERR_IO;
        }
    }

  return ZX_OK;
}

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
    auto partitioner = DevicePartitioner::Create(devfs_root_.duplicate(), arch,
                                                 std::move(block_device));
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
    auto partitioner = DevicePartitioner::Create(devfs_root_.duplicate(), arch,
                                                 std::move(block_device));
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

}  //  namespace paver
