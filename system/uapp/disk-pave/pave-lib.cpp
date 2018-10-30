// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <fs-management/ramdisk.h>
#include <fvm/fvm-lz4.h>
#include <fvm/fvm-sparse.h>
#include <lib/cksum.h>
#include <lib/fzl/fdio.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/fifo.h>
#include <lib/zx/vmo.h>
#include <zircon/boot/image.h>
#include <zircon/device/block.h>
#include <zircon/device/device.h>
#include <zircon/skipblock/c/fidl.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zxcrypt/volume.h>

#include "fvm/fvm-sparse.h"
#include "fvm/fvm.h"
#include "pave-lib.h"
#include "pave-logging.h"
#include "pave-utils.h"

#define ZXCRYPT_DRIVER_LIB "/boot/driver/zxcrypt.so"

namespace paver {
namespace {

static Partition PartitionType(const Command command) {
    switch (command) {
    case Command::kInstallBootloader:
        return Partition::kBootloader;
    case Command::kInstallEfi:
        return Partition::kEfi;
    case Command::kInstallKernc:
        return Partition::kKernelC;
    case Command::kInstallZirconA:
        return Partition::kZirconA;
    case Command::kInstallZirconB:
        return Partition::kZirconB;
    case Command::kInstallZirconR:
        return Partition::kZirconR;
    case Command::kInstallFvm:
        return Partition::kFuchsiaVolumeManager;
    default:
        return Partition::kUnknown;
    }
}

// The number of additional slices a partition will need to become
// zxcrypt'd.
//
// TODO(aarongreen): Replace this with a value supplied by ulib/zxcrypt.
constexpr size_t kZxcryptExtraSlices = 1;

// Confirm that the file descriptor to the underlying partition exists within an
// FVM, not, for example, a GPT or MBR.
//
// |out| is true if |fd| is a VPartition, else false.
zx_status_t FvmIsVirtualPartition(const fbl::unique_fd& fd, bool* out) {
    char path[PATH_MAX];
    const ssize_t r = ioctl_device_get_topo_path(fd.get(), path, sizeof(path));
    if (r < 0) {
        return ZX_ERR_IO;
    }

    *out = strstr(path, "fvm") != nullptr;
    return ZX_OK;
}

// Describes the state of a partition actively being written
// out to disk.
struct PartitionInfo {
    PartitionInfo()
        : pd(nullptr) {}

    fvm::partition_descriptor_t* pd;
    fbl::unique_fd new_part;
    fbl::unique_fd old_part; // Or '-1' if this is a new partition
};

inline fvm::extent_descriptor_t* GetExtent(fvm::partition_descriptor_t* pd, size_t extent) {
    return reinterpret_cast<fvm::extent_descriptor_t*>(
        reinterpret_cast<uintptr_t>(pd) + sizeof(fvm::partition_descriptor_t) +
        extent * sizeof(fvm::extent_descriptor_t));
}

// Registers a FIFO
zx_status_t RegisterFastBlockIo(const fbl::unique_fd& fd, const zx::vmo& vmo,
                                vmoid_t* vmoid_out, block_client::Client* client_out) {
    zx::fifo fifo;
    if (ioctl_block_get_fifos(fd.get(), fifo.reset_and_get_address()) < 0) {
        ERROR("Couldn't attach fifo to partition\n");
        return ZX_ERR_IO;
    }
    zx::vmo dup;
    if (vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup) != ZX_OK) {
        ERROR("Couldn't duplicate buffer vmo\n");
        return ZX_ERR_IO;
    }
    zx_handle_t h = dup.release();
    if (ioctl_block_attach_vmo(fd.get(), &h, vmoid_out) < 0) {
        ERROR("Couldn't attach VMO\n");
        return ZX_ERR_IO;
    }
    return block_client::Client::Create(fbl::move(fifo), client_out);
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
            zx_status_t status = reader->ReadData(
                &reinterpret_cast<uint8_t*>(mapper.start())[vmo_sz],
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

// Stream a raw (non-FVM) partition to a vmo.
zx_status_t StreamPayloadToVmo(fzl::ResizeableVmoMapper& mapper, const fbl::unique_fd& src_fd,
                               uint32_t block_size_bytes, size_t* payload_size) {
    zx_status_t status;
    ssize_t r;
    size_t vmo_offset = 0;

    while ((r = read(src_fd.get(), &reinterpret_cast<uint8_t*>(mapper.start())[vmo_offset],
                     mapper.size() - vmo_offset)) > 0) {
        vmo_offset += r;
        if (mapper.size() - vmo_offset == 0) {
            // The buffer is full, let's grow the VMO.
            if ((status = mapper.Grow(mapper.size() << 1)) != ZX_OK) {
                ERROR("Failed to grow VMO\n");
                return status;
            }
        }
    }

    if (r < 0) {
        ERROR("Error reading partition data\n");
        return static_cast<zx_status_t>(r);
    }

    if (vmo_offset % block_size_bytes) {
        // We have a partial block to write.
        const size_t rounded_length = fbl::round_up(vmo_offset, block_size_bytes);
        memset(&reinterpret_cast<uint8_t*>(mapper.start())[vmo_offset], 0,
               rounded_length - vmo_offset);
        vmo_offset = rounded_length;
    }
    *payload_size = vmo_offset;
    return ZX_OK;
}

// Writes a raw (non-FVM) partition to a block device from a VMO.
zx_status_t WriteVmoToBlock(const zx::vmo& vmo, size_t vmo_size,
                            const fbl::unique_fd& partition_fd, uint32_t block_size_bytes) {
    ZX_ASSERT(vmo_size % block_size_bytes == 0);

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
                                const fzl::FdioCaller& caller, uint32_t block_size_bytes) {
    ZX_ASSERT(vmo_size % block_size_bytes == 0);

    zx::vmo dup;
    zx_status_t status;
    if ((status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup)) != ZX_OK) {
        ERROR("Couldn't duplicate buffer vmo\n");
        return status;
    }

    zircon_skipblock_ReadWriteOperation operation = {
        .vmo = dup.release(),
        .vmo_offset = 0,
        .block = 0,
        .block_count = static_cast<uint32_t>(vmo_size / block_size_bytes),
    };
    bool bad_block_grown;

    zircon_skipblock_SkipBlockWrite(caller.borrow_channel(), &operation, &status, &bad_block_grown);
    if (status != ZX_OK) {
        ERROR("Error writing partition data: %s\n", zx_status_get_string(status));
        return status;
    }
    return ZX_OK;
}

// Checks first few bytes of buffer to ensure it is a ZBI.
// Also validates architecture in kernel header matches the target.
bool ValidateKernelZbi(const uint8_t* buffer, size_t size, Arch arch) {
    const auto payload = reinterpret_cast<const zircon_kernel_t*>(buffer);
    const uint32_t expected_kernel = (arch == Arch::X64) ? ZBI_TYPE_KERNEL_X64
                                                         : ZBI_TYPE_KERNEL_ARM64;

    const auto crc_valid = [](const zbi_header_t* hdr) {
        const uint32_t crc = crc32(0, reinterpret_cast<const uint8_t*>(hdr + 1),
                                   hdr->length);
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

// Attempt to bind an FVM driver to a partition fd.
fbl::unique_fd TryBindToFvmDriver(const fbl::unique_fd& partition_fd,
                                  zx::duration timeout) {
    char path[PATH_MAX];
    ssize_t r = ioctl_device_get_topo_path(partition_fd.get(), path, sizeof(path));
    if (r < 0) {
        ERROR("Failed to get topological path\n");
        return fbl::unique_fd();
    }

    constexpr char kFvmDriverLib[] = "/boot/driver/fvm.so";
    r = ioctl_device_bind(partition_fd.get(), kFvmDriverLib, sizeof(kFvmDriverLib));
    if (r < 0) {
        ERROR("Could not bind fvm driver\n");
        return fbl::unique_fd();
    }

    char fvm_path[PATH_MAX];
    snprintf(fvm_path, sizeof(fvm_path), "%s/fvm", path);
    if (wait_for_device(fvm_path, timeout.get()) != ZX_OK) {
        ERROR("Error waiting for fvm driver to bind\n");
        return fbl::unique_fd();
    }
    return fbl::unique_fd(open(fvm_path, O_RDWR));
}

// Options for locating an FVM within a partition.
enum class BindOption {
    // Bind to the FVM, if it exists already.
    TryBind,
    // Reformat the partition, regardless of if it already exists as an FVM.
    Reformat,
};

// Formats the FVM within the provided partition if it is not already formatted.
//
// On success, returns a file descriptor to an FVM.
// On failure, returns -1
fbl::unique_fd FvmPartitionFormat(fbl::unique_fd partition_fd, size_t slice_size,
                                  BindOption option) {
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
            fvm_fd = TryBindToFvmDriver(partition_fd, zx::sec(3));
            if (fvm_fd) {
                LOG("Found already formatted FVM.\n");
                fvm_info_t info;
                ssize_t r = ioctl_block_fvm_query(fvm_fd.get(), &info);
                if (r >= 0) {
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

    ssize_t r = ioctl_block_rr_part(partition_fd.get());
    if (r < 0) {
        ERROR("Could not rebind partition: %s\n",
              zx_status_get_string(static_cast<zx_status_t>(r)));
        return fbl::unique_fd();
    }

    return TryBindToFvmDriver(partition_fd, zx::sec(3));
}

// Formats a block device as a zxcrypt volume.
//
// On success, returns a file descriptor to an FVM.
// On failure, returns -1
zx_status_t ZxcryptCreate(PartitionInfo* part) {
    zx_status_t status;

    char path[PATH_MAX];
    ssize_t r;
    if ((r = ioctl_device_get_topo_path(part->new_part.get(), path, sizeof(path))) < 0) {
        status = static_cast<zx_status_t>(r);
        ERROR("Failed to get topological path\n");
        return status;
    }
    // TODO(security): ZX-1130. We need to bind with channel in order to pass a key here.
    // TODO(security): ZX-1864. The created volume must marked as needing key rotation.
    crypto::Secret key;
    uint8_t* tmp;
    if ((status = key.Allocate(zxcrypt::kZx1130KeyLen, &tmp)) != ZX_OK) {
        return status;
    }
    memset(tmp, 0, key.len());

    fbl::unique_ptr<zxcrypt::Volume> volume;
    if ((status = zxcrypt::Volume::Create(fbl::move(part->new_part), key, &volume)) != ZX_OK ||
        (status = volume->Open(zx::sec(3), &part->new_part)) != ZX_OK) {
        ERROR("Could not create zxcrypt volume\n");
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
    extend_request_t req;
    req.offset = allocated - reserved;
    req.length = needed - allocated;

    if ((r = ioctl_block_fvm_extend(part->new_part.get(), &req)) < 0) {
        status = static_cast<zx_status_t>(r);
        ERROR("Failed to extend zxcrypt volume: %s\n", zx_status_get_string(status));
        return status;
    }

    return ZX_OK;
}

// Returns |ZX_OK| if |partition_fd| is a child of |fvm_fd|.
zx_status_t FvmPartitionIsChild(const fbl::unique_fd& fvm_fd, const fbl::unique_fd& partition_fd) {
    char fvm_path[PATH_MAX];
    char part_path[PATH_MAX];
    ssize_t r;
    if ((r = ioctl_device_get_topo_path(fvm_fd.get(), fvm_path, sizeof(fvm_path))) < 0) {
        ERROR("Couldn't get topological path of FVM\n");
        return static_cast<zx_status_t>(r);
    } else if ((r = ioctl_device_get_topo_path(partition_fd.get(), part_path,
                                               sizeof(part_path))) < 0) {
        ERROR("Couldn't get topological path of partition\n");
        return static_cast<zx_status_t>(r);
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

// Calculate the amount of space necessary for the incoming partitions,
// validating the header along the way.
//
// Parses the information from the |reader| into |parts|.
zx_status_t ValidatePartitions(const fbl::unique_fd& fvm_fd,
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

        parts[p].old_part.reset(open_partition(nullptr, parts[p].pd->type, ZX_SEC(2), nullptr));
        if (parts[p].old_part) {
            bool is_vpartition;
            if (FvmIsVirtualPartition(parts[p].old_part, &is_vpartition) != ZX_OK) {
                ERROR("Couldn't confirm old vpartition type\n");
                return ZX_ERR_IO;
            }
            if (FvmPartitionIsChild(fvm_fd, parts[p].old_part) != ZX_OK) {
                RecommendWipe("Streaming a partition type which also exists outside FVM");
                return ZX_ERR_BAD_STATE;
            }
            if (!is_vpartition) {
                RecommendWipe("Streaming a partition type which also exists in a GPT");
                return ZX_ERR_BAD_STATE;
            }
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
        part = reinterpret_cast<fvm::partition_descriptor*>(
            reinterpret_cast<uintptr_t>(ext) + sizeof(fvm::extent_descriptor_t));
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
        alloc.flags = fvm::kVPartFlagInactive;
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
            extend_request_t request;
            request.offset = ext->slice_start;
            request.length = ext->slice_count;
            ssize_t result = ioctl_block_fvm_extend(parts[p].new_part.get(), &request);
            if (result < 0) {
                ERROR("Failed to extend partition: %s\n",
                      zx_status_get_string(static_cast<zx_status_t>(result)));
                return ZX_ERR_NO_SPACE;
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
zx_status_t FvmStreamPartitions(fbl::unique_fd partition_fd, fbl::unique_fd src_fd) {
    fbl::unique_ptr<fvm::SparseReader> reader;
    zx_status_t status;
    if ((status = fvm::SparseReader::Create(fbl::move(src_fd), &reader)) != ZX_OK) {
        return status;
    }

    LOG("Header Validated - OK\n");
    // Duplicate the partition fd; we may need it later if we reformat the FVM.
    fbl::unique_fd partition_fd2(dup(partition_fd.get()));
    if (!partition_fd2) {
        ERROR("Coudln't dup partition fd\n");
        return ZX_ERR_IO;
    }

    fvm::sparse_image_t* hdr = reader->Image();
    // Acquire an fd to the FVM, either by finding one that already
    // exists, or formatting a new one.
    fbl::unique_fd fvm_fd(FvmPartitionFormat(fbl::move(partition_fd2), hdr->slice_size,
                                             BindOption::TryBind));
    if (!fvm_fd) {
        ERROR("Couldn't find FVM partition\n");
        return ZX_ERR_IO;
    }

    fbl::Array<PartitionInfo> parts(new PartitionInfo[hdr->partition_count],
                                    hdr->partition_count);

    // Parse the incoming image and calculate its size.
    size_t requested_slices = 0;
    if ((status = ValidatePartitions(fvm_fd, reader, parts, &requested_slices)) != ZX_OK) {
        ERROR("Failed to validate partitions: %s\n", zx_status_get_string(status));
        return status;
    }

    // Contend with issues from an image that may be too large for this device.
    fvm_info_t info;
    ssize_t result = ioctl_block_fvm_query(fvm_fd.get(), &info);
    if (result < 0) {
        zx_status_t status = static_cast<zx_status_t>(result);
        ERROR("Failed to acquire FVM info: %s\n", zx_status_get_string(status));
        return status;
    }
    size_t free_slices = info.pslice_total_count - info.pslice_allocated_count;
    if (info.pslice_total_count < requested_slices) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Image size (%zu) > Storage size (%zu)",
                 requested_slices * hdr->slice_size,
                 info.pslice_total_count * hdr->slice_size);
        Warn(buf, "Image is too large to be paved to device");
        return ZX_ERR_NO_SPACE;
    }
    if (free_slices < requested_slices) {
        Warn("Not enough space to non-destructively pave",
             "Automatically reinitializing FVM; Expect data loss");
        // Shut down the connections to the old partitions; they will
        // become defunct when the FVM is re-initialized.
        for (size_t p = 0; p < parts.size(); p++) {
            parts[p].old_part.reset();
        }

        fvm_fd = FvmPartitionFormat(fbl::move(partition_fd), hdr->slice_size,
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
    if ((status = mapping.CreateAndMap(vmo_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                       nullptr, &vmo)) != ZX_OK) {
        ERROR("Failed to create stream VMO\n");
        return ZX_ERR_NO_MEMORY;
    }

    // Now that all partitions are preallocated, begin streaming data to them.
    for (size_t p = 0; p < parts.size(); p++) {
        vmoid_t vmoid;
        block_client::Client client;
        zx_status_t status = RegisterFastBlockIo(parts[p].new_part, vmo, &vmoid, &client);
        if (status != ZX_OK) {
            ERROR("Failed to register fast block IO\n");
            return status;
        }

        block_info_t binfo;
        if ((ioctl_block_get_info(parts[p].new_part.get(), &binfo)) < 0) {
            ERROR("Couldn't get partition block info\n");
            return ZX_ERR_IO;
        }
        size_t block_size = binfo.block_size;

        block_fifo_request_t request;
        request.group = 0;
        request.vmoid = vmoid;
        request.opcode = BLOCKIO_WRITE;

        LOG("Streaming partition %zu\n", p);
        status = StreamFvmPartition(reader.get(), &parts[p], mapping, client, block_size,
                                    &request);
        LOG("Done streaming partition %zu\n", p);
        if (status != ZX_OK) {
            ERROR("Failed to stream partition\n");
            return status;
        }
        if ((status = FlushClient(client)) != ZX_OK) {
            ERROR("Failed to flush client\n");
            return status;
        }
        LOG("Done flushing partition %zu\n", p);
    }

    for (size_t p = 0; p < parts.size(); p++) {
        // Upgrade the old partition (currently active) to the new partition (currently
        // inactive), so when the new partition becomes active, the old
        // partition is destroyed.
        upgrade_req_t upgrade;
        memset(&upgrade, 0, sizeof(upgrade));
        if (parts[p].old_part) {
            if (ioctl_block_get_partition_guid(parts[p].old_part.get(), &upgrade.old_guid,
                                               GUID_LEN) < 0) {
                ERROR("Failed to get unique GUID of old partition\n");
                return ZX_ERR_BAD_STATE;
            }
        }
        if (ioctl_block_get_partition_guid(parts[p].new_part.get(), &upgrade.new_guid,
                                           GUID_LEN) < 0) {
            ERROR("Failed to get unique GUID of new partition\n");
            return ZX_ERR_BAD_STATE;
        }

        if (ioctl_block_fvm_upgrade(fvm_fd.get(), &upgrade) < 0) {
            ERROR("Failed to upgrade partition\n");
            return ZX_ERR_IO;
        }

        if (parts[p].old_part) {
            // This would fail if the old part was on GPT, not FVM. However,
            // we checked earlier and verified that parts[p].old_part, if it exists,
            // is a vpartition.
            ssize_t r = ioctl_block_fvm_destroy_partition(parts[p].old_part.get());
            if (r < 0) {
                ERROR("Couldn't destroy partition: %ld\n", r);
                return static_cast<zx_status_t>(r);
            }
        }
    }

    return ZX_OK;
}

} // namespace

zx_status_t PartitionPave(fbl::unique_ptr<DevicePartitioner> partitioner,
                          fbl::unique_fd payload_fd, Partition partition_type, Arch arch) {
    LOG("Paving partition.\n");

    zx_status_t status;
    fbl::unique_fd partition_fd;
    if ((status = partitioner->FindPartition(partition_type, &partition_fd)) != ZX_OK) {
        if (status != ZX_ERR_NOT_FOUND) {
            ERROR("Failure looking for partition: %s\n", zx_status_get_string(status));
            return status;
        }
        if ((status = partitioner->AddPartition(partition_type, &partition_fd)) != ZX_OK) {
            ERROR("Failure creating partition: %s\n", zx_status_get_string(status));
            return status;
        }
    } else {
        LOG("Partition already exists\n");
    }

    if (partition_type == Partition::kFuchsiaVolumeManager) {
        if (partitioner->UseSkipBlockInterface()) {
            LOG("Attempting to format FTL...\n");
            status = partitioner->WipePartitions();
            if (status != ZX_OK) {
                ERROR("Failed to format FTL: %s\n", zx_status_get_string(status));
            } else {
                LOG("Formatted successfully!\n");
            }
        }
        LOG("Streaming partitions...\n");
        if ((status = FvmStreamPartitions(fbl::move(partition_fd), fbl::move(payload_fd))) != ZX_OK) {
            ERROR("Failed to stream partitions: %s\n", zx_status_get_string(status));
            return status;
        }
        LOG("Completed successfully\n");
        return ZX_OK;
    }

    uint32_t block_size_bytes;
    if ((status = partitioner->GetBlockSize(partition_fd, &block_size_bytes)) != ZX_OK) {
        ERROR("Couldn't get partition block size\n");
        return status;
    }

    const size_t vmo_sz = fbl::round_up(1LU << 20, block_size_bytes);
    fzl::ResizeableVmoMapper mapper;
    if ((status = mapper.CreateAndMap(vmo_sz, "partition-pave")) != ZX_OK) {
        ERROR("Failed to create stream VMO\n");
        return status;
    }
    // The streamed partition size may not line up with the mapped vmo size.
    size_t payload_size = 0;
    if ((status = StreamPayloadToVmo(mapper, payload_fd, block_size_bytes,
                                     &payload_size)) != ZX_OK) {
        ERROR("Failed to stream partition to VMO\n");
        return status;
    }
    if ((status = ValidateKernelPayload(mapper, payload_size, partition_type, arch)) != ZX_OK) {
        ERROR("Failed to validate partition\n");
        return status;
    }
    if (partitioner->UseSkipBlockInterface()) {
        fzl::FdioCaller caller(fbl::move(partition_fd));
        status = WriteVmoToSkipBlock(mapper.vmo(), payload_size, caller, block_size_bytes);
        partition_fd = caller.release();
    } else {
        status = WriteVmoToBlock(mapper.vmo(), payload_size, partition_fd, block_size_bytes);
    }
    if (status != ZX_OK) {
        ERROR("Failed to write partition to block\n");
        return status;
    }

    if ((status = partitioner->FinalizePartition(partition_type)) != ZX_OK) {
        ERROR("Failed to finalize partition\n");
        return status;
    }

    LOG("Completed successfully\n");
    return ZX_OK;
}

void Drain(fbl::unique_fd fd) {
    char buf[8192];
    while (read(fd.get(), &buf, sizeof(buf)) > 0)
        continue;
}

zx_status_t RealMain(Flags flags) {
    auto device_partitioner = DevicePartitioner::Create();
    if (!device_partitioner) {
        ERROR("Unable to initialize a partitioner.");
        return ZX_ERR_BAD_STATE;
    }
    const bool is_cros_device = device_partitioner->IsCros();

    switch (flags.cmd) {
    case Command::kWipe:
        return device_partitioner->WipePartitions();
    case Command::kInstallFvm:
        break;
    case Command::kInstallBootloader:
        if (flags.arch == Arch::X64 && !flags.force) {
            LOG("SKIPPING BOOTLOADER install on x64 device, pass --force if desired.\n");
            Drain(fbl::move(flags.payload_fd));
            return ZX_OK;
        }
        break;
    case Command::kInstallEfi:
        if ((is_cros_device || flags.arch == Arch::ARM64) && !flags.force) {
            LOG("SKIPPING EFI install on ARM64/CROS device, pass --force if desired.\n");
            Drain(fbl::move(flags.payload_fd));
            return ZX_OK;
        }
        break;
    case Command::kInstallKernc:
        if (!is_cros_device && !flags.force) {
            LOG("SKIPPING KERNC install on non-CROS device, pass --force if desired.\n");
            Drain(fbl::move(flags.payload_fd));
            return ZX_OK;
        }
        break;
    case Command::kInstallZirconA:
    case Command::kInstallZirconB:
    case Command::kInstallZirconR:
        if (is_cros_device && !flags.force) {
            LOG("SKIPPING Zircon-{A/B/R} install on CROS device, pass --force if desired.\n");
            Drain(fbl::move(flags.payload_fd));
            return ZX_OK;
        }
        break;
    case Command::kInstallDataFile:
        return DataFilePave(fbl::move(device_partitioner), fbl::move(flags.payload_fd), flags.path);

    default:
        ERROR("Unsupported command.");
        return ZX_ERR_NOT_SUPPORTED;
    }
    return PartitionPave(fbl::move(device_partitioner), fbl::move(flags.payload_fd),
                         PartitionType(flags.cmd), flags.arch);
}

zx_status_t DataFilePave(fbl::unique_ptr<DevicePartitioner> partitioner,
                         fbl::unique_fd payload_fd, char* data_path) {

    const char* mount_path = "/volume/data";
    const uint8_t data_guid[] = GUID_DATA_VALUE;
    char minfs_path[PATH_MAX] = {0};
    char path[PATH_MAX] = {0};
    zx_status_t status = ZX_OK;

    fbl::unique_fd part_fd(open_partition(nullptr, data_guid, ZX_SEC(1), path));
    if (!part_fd) {
        ERROR("DATA partition not found in FVM\n");
        Drain(fbl::move(payload_fd));
        return ZX_ERR_NOT_FOUND;
    }

    switch (detect_disk_format(part_fd.get())) {
    case DISK_FORMAT_MINFS:
        // If the disk we found is actually minfs, we can just use the block
        // device path we were given by open_partition.
        strncpy(minfs_path, path, PATH_MAX);
        break;

    case DISK_FORMAT_ZXCRYPT:
        // Compute the topological path of the FVM block driver, and then tack
        // the zxcrypt-device string onto the end. This should be improved.
        ioctl_device_get_topo_path(part_fd.get(), path, sizeof(path));
        snprintf(minfs_path, sizeof(minfs_path), "%s/zxcrypt/block", path);

        // TODO(security): ZX-1130. We need to bind with channel in order to
        // pass a key here. Where does the key come from? We need to determine
        // if this is unattended.
        ioctl_device_bind(part_fd.get(), ZXCRYPT_DRIVER_LIB, strlen(ZXCRYPT_DRIVER_LIB));

        if ((status = wait_for_device(minfs_path, ZX_SEC(5))) != ZX_OK) {
            ERROR("zxcrypt bind error: %s\n", zx_status_get_string(status));
            return status;
        }

        break;

    default:
        ERROR("unsupported disk format at %s\n", path);
        return ZX_ERR_NOT_SUPPORTED;
    }

    mount_options_t opts(default_mount_options);
    opts.create_mountpoint = true;
    if ((status = mount(open(minfs_path, O_RDWR), mount_path, DISK_FORMAT_MINFS,
                        &opts, launch_logs_async)) != ZX_OK) {
        ERROR("mount error: %s\n", zx_status_get_string(status));
        Drain(fbl::move(payload_fd));
        return status;
    }

    // mkdir any intermediate directories between mount_path and basename(data_path)
    snprintf(path, sizeof(path), "%s/%s", mount_path, data_path);
    size_t cur = strlen(mount_path);
    size_t max = strlen(path) - strlen(basename(path));
    // note: the call to basename above modifies path, so it needs reconstruction.
    snprintf(path, sizeof(path), "%s/%s", mount_path, data_path);
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
        char buf[8192];
        ssize_t n;
        fbl::unique_fd kfd(open(path, O_CREAT | O_WRONLY | O_APPEND, 0600));
        if (!kfd) {
            umount(mount_path);
            ERROR("open %s error: %s\n", data_path, strerror(errno));
            Drain(fbl::move(payload_fd));
            return ZX_ERR_IO;
        }
        while ((n = read(payload_fd.get(), &buf, sizeof(buf))) > 0) {
            if (write(kfd.get(), &buf, n) != n) {
                umount(mount_path);
                ERROR("write %s error: %s\n", data_path, strerror(errno));
                Drain(fbl::move(payload_fd));
                return ZX_ERR_IO;
            }
        }
        fsync(kfd.get());
    }

    if ((status = umount(mount_path)) != ZX_OK) {
        ERROR("unmount %s failed: %s\n", mount_path,
            zx_status_get_string(status));
        return status;
    }

    LOG("Wrote %s\n", data_path);
    return ZX_OK;
}

} //  namespace paver
