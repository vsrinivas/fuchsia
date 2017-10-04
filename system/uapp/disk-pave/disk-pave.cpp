// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __Fuchsia__
#include <fs/mapped-vmo.h>
#include <fs-management/mount.h>
#include <fs-management/ramdisk.h>
#include <zircon/device/device.h>
#include <zx/vmo.h>
#endif

#include <block-client/client.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fdio/debug.h>
#include <fdio/watcher.h>
#include <fs/mapped-vmo.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <zx/fifo.h>
#include <zx/vmo.h>

#include "fvm/fvm.h"
#include "fvm/fvm-sparse.h"

#define MXDEBUG 0

#define FVM_DRIVER_LIB "/boot/driver/fvm.so"
#define STRLEN(s) sizeof(s) / sizeof((s)[0])

namespace {

constexpr char kBlockDevPath[] = "/dev/class/block";

// Confirm that the file descriptor to the underlying partition exists within an
// FVM, not, for example, a GPT or MBR.
//
// |out| is true if |fd| is a VPartition, else false.
zx_status_t fvm_is_vpartition(int fd, bool* out) {
    char path[PATH_MAX];
    ssize_t r = ioctl_device_get_topo_path(fd, path, sizeof(path));
    if (r < 0) {
        return ZX_ERR_IO;
    }

    if (strstr(path, "fvm") != nullptr) {
        *out = true;
    } else {
        *out = false;
    }
    return ZX_OK;
}

// Describes the state of a partition actively being written
// out to disk.
struct partition_info {
    fvm::partition_descriptor_t* pd;
    fbl::unique_fd new_part;
    fbl::unique_fd old_part; // Or '-1' if this is a new partition
};

inline fvm::extent_descriptor_t* get_extent(fvm::partition_descriptor_t* pd, size_t extent) {
    return reinterpret_cast<fvm::extent_descriptor_t*>(
            reinterpret_cast<uintptr_t>(pd) + sizeof(fvm::partition_descriptor_t) +
            extent * sizeof(fvm::extent_descriptor_t));
}

zx_status_t register_fast_block_io(int fd, zx_handle_t vmo,
                                   txnid_t* txnid_out, vmoid_t* vmoid_out,
                                   fifo_client_t** client_out) {
    zx::fifo fifo;
    if (ioctl_block_get_fifos(fd, fifo.reset_and_get_address()) < 0) {
        fprintf(stderr, "[register_fast_block_io] Couldn't attach fifo to partition\n");
        return ZX_ERR_IO;
    }
    if (ioctl_block_alloc_txn(fd, txnid_out) < 0) {
        fprintf(stderr, "[register_fast_block_io] Couldn't allocate transaction\n");
        return ZX_ERR_IO;
    }
    zx::vmo dup;
    if (zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS,
                            dup.reset_and_get_address()) != ZX_OK) {
        fprintf(stderr, "[register_fast_block_io] Couldn't duplicate buffer vmo\n");
        return ZX_ERR_IO;
    }
    zx_handle_t h = dup.release();
    if (ioctl_block_attach_vmo(fd, &h, vmoid_out) < 0) {
        fprintf(stderr, "[register_fast_block_io] Couldn't attach VMO\n");
        return ZX_ERR_IO;
    }
    if (block_fifo_create_client(fifo.release(), client_out) != ZX_OK) {
        fprintf(stderr, "[register_fast_block_io] Couldn't create block client\n");
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

// Stream an FVM partition to disk.
zx_status_t stream_fvm_partition(partition_info* part, MappedVmo* mvmo,
                                 fifo_client_t* client, size_t slice_size,
                                 block_fifo_request_t* request, int src_fd) {
    const size_t vmo_cap = mvmo->GetSize();
    for (size_t e = 0; e < part->pd->extent_count; e++) {
        printf("[stream_fvm_partition] Writing extent %lu... \n", e);
        fvm::extent_descriptor_t* ext = get_extent(part->pd, e);
        size_t offset = ext->slice_start * slice_size;
        size_t bytes_left = ext->extent_length;

        // Write real data
        while (bytes_left > 0) {
            ssize_t r;
            size_t vmo_sz = 0;
            while ((r = read(src_fd, &reinterpret_cast<uint8_t*>(mvmo->GetData())[vmo_sz],
                             fbl::min(bytes_left, vmo_cap - vmo_sz))) > 0) {
                vmo_sz += r;
                bytes_left -= r;
                if (bytes_left == 0) {
                    break;
                }
            }
            if (vmo_sz == 0) {
                fprintf(stderr, "[stream_fvm_partition] Read nothing from src_fd; %lu bytes left\n",
                        bytes_left);
                return ZX_ERR_IO;
            }
            if (r < 0) {
                fprintf(stderr, "[stream_fvm_partition] Error reading partition data\n");
                return static_cast<zx_status_t>(r);
            }

            request->length = vmo_sz;
            request->vmo_offset = 0;
            request->dev_offset = offset;

            if ((r = block_fifo_txn(client, request, 1)) != ZX_OK) {
                fprintf(stderr, "[stream_fvm_partition] Error writing partition data\n");
                return static_cast<zx_status_t>(r);
            }

            offset += request->length;
        }

        // Write trailing zeroes (which are implied, but were omitted from
        // transfer).
        bytes_left = (ext->slice_count * slice_size) - ext->extent_length;
        if (bytes_left > 0) {
            printf("[stream_fvm_partition] %lu bytes written, %lu zeroes left\n",
                   ext->extent_length, bytes_left);
            memset(mvmo->GetData(), 0, vmo_cap);
        }
        while(bytes_left > 0) {
            request->length = fbl::min(bytes_left, vmo_cap);
            request->vmo_offset = 0;
            request->dev_offset = offset;

            zx_status_t status;
            if ((status = block_fifo_txn(client, request, 1)) != ZX_OK) {
                fprintf(stderr, "[stream_fvm_partition] Error writing trailing zeroes\n");
                return status;
            }

            offset += request->length;
            bytes_left -= request->length;
        }
    }
    return ZX_OK;
}

// Stream a raw (non-FVM) partition to disk.
zx_status_t stream_partition(MappedVmo* mvmo, fifo_client_t* client,
                             block_fifo_request_t* request, int src_fd) {
    const size_t vmo_cap = mvmo->GetSize();
    size_t offset = 0;

    while (true) {
        ssize_t r;
        size_t vmo_sz = 0;
        while ((r = read(src_fd, &reinterpret_cast<uint8_t*>(mvmo->GetData())[vmo_sz],
                         vmo_cap - vmo_sz)) > 0) {
            vmo_sz += r;
            if (vmo_cap - vmo_sz == 0) {
                // The buffer is full, let's write to disk.
                break;
            }
        }
        if (r < 0) {
            fprintf(stderr, "[stream_partition] Error reading partition data\n");
            return static_cast<zx_status_t>(r);
        }
        if (vmo_sz == 0 || r == 0) {
            // Nothing left to write
            return ZX_OK;
        }

        request->length = vmo_sz;
        request->vmo_offset = 0;
        request->dev_offset = offset;

        if ((r = block_fifo_txn(client, request, 1)) != ZX_OK) {
            fprintf(stderr, "[stream_partition] Error writing partition data\n");
            return static_cast<zx_status_t>(r);
        }

        offset += request->length;
    }
}

// Finds a partition with "FVM type GUID" within a GPT,
// and formats the FVM within the GPT if it is not already
// formatted.
//
// On success, returns a file descriptor to an FVM.
// On failure, returns -1
fbl::unique_fd fvm_find_or_format(size_t slice_size) {
    const uint8_t type[GPT_GUID_LEN] = GUID_FVM_VALUE;
    fbl::unique_fd fd(open_partition(nullptr, type, 0, nullptr));
    if (!fd) {
        fprintf(stderr, "[fvm_find_or_format] Couldn't find a GPT partition for FVM\n");
        return fbl::unique_fd();
    }

    disk_format_t df = detect_disk_format(fd.get());
    if (df != DISK_FORMAT_FVM) {
        printf("[fvm_find_or_format] Initializing partition as FVM\n");
        if (fvm_init(fd.get(), slice_size)) {
            fprintf(stderr, "[fvm_find_or_format] Failed to initialize fvm\n");
            return fbl::unique_fd();
        }
    }
    char path[PATH_MAX];
    ssize_t r = ioctl_device_get_topo_path(fd.get(), path, sizeof(path));
    if (r < 0) {
        fprintf(stderr, "[fvm_find_or_format] Failed to get topological path\n");
        return fbl::unique_fd();
    }

    r = ioctl_device_bind(fd.get(), FVM_DRIVER_LIB, STRLEN(FVM_DRIVER_LIB));
    if (r < 0) {
        fprintf(stderr, "[fvm_find_or_format] Could not bind fvm driver\n");
        return fbl::unique_fd();
    }

    if (wait_for_driver_bind(path, "fvm")) {
        fprintf(stderr, "[fvm_find_or_format]: Error waiting for fvm driver to bind\n");
        return fbl::unique_fd();
    }
    strcat(path, "/fvm");
    return fbl::unique_fd(open(path, O_RDWR));
}

// Returns |ZX_OK| if |part_fd| is a child of |fvm_fd|.
zx_status_t fvm_partition_match(int fvm_fd, int part_fd) {
    char fvm_path[PATH_MAX];
    char part_path[PATH_MAX];
    ssize_t r;
    if ((r = ioctl_device_get_topo_path(fvm_fd, fvm_path, sizeof(fvm_path))) < 0) {
        fprintf(stderr, "[fvm_partition_match] Couldn't get topological path of FVM\n");
        return static_cast<zx_status_t>(r);
    } else if ((r = ioctl_device_get_topo_path(part_fd, part_path, sizeof(part_path))) < 0) {
        fprintf(stderr, "[fvm_partition_match] Couldn't get topological path of partition\n");
        return static_cast<zx_status_t>(r);
    }
    if (strncmp(fvm_path, part_path, strlen(fvm_path))) {
        fprintf(stderr, "[fvm_partition_match] Partition does not exist within FVM\n");
        return ZX_ERR_BAD_STATE;
    }
    return ZX_OK;
}

// Given an fd representing a "sparse FVM format", fill the FVM with the
// provided partitions described by |src_fd|.
//
// Decides to overwrite or create new partitions based on the type
// GUID, not the instance GUID.
zx_status_t fvm_stream_partitions(fbl::unique_fd src_fd) {
    fvm::sparse_image_t hdr;
    if (read(src_fd.get(), &hdr, sizeof(hdr)) != sizeof(hdr)) {
        fprintf(stderr, "[fvm_stream_partitions] Failed to read the sparse header\n");
        return ZX_ERR_IO;
    }

    // Verify the header, then allocate and stream the remaining metadata
    if (hdr.magic != fvm::kSparseFormatMagic) {
        fprintf(stderr, "[fvm_stream_partitions] Bad magic\n");
        return ZX_ERR_IO;
    } else if (hdr.version != fvm::kSparseFormatVersion) {
        fprintf(stderr, "[fvm_stream_partitions] Unexpected sparse file version\n");
        return ZX_ERR_IO;
    }

    printf("[fvm_stream_partitions] Header Validated - OK\n");

    // Acquire an fd to the fvm, either by finding one that already
    // exists, or creating a new one.
    fbl::unique_fd fvm_fd(fvm_find_or_format(hdr.slice_size));
    if (!fvm_fd) {
        fprintf(stderr, "[fvm_stream_partitions] Couldn't find FVM partition\n");
        return ZX_ERR_IO;
    }

    // TODO(smklein): In this case, we could actually unbind the FVM driver,
    // create a new FVM with the updated slice size, and rebind.
    fvm_info_t info;
    if (ioctl_block_fvm_query(fvm_fd.get(), &info) < 0) {
        fprintf(stderr, "[fvm_stream_partitions] Couldn't query underlying FVM\n");
        return ZX_ERR_IO;
    } else if (info.slice_size != hdr.slice_size) {
        fprintf(stderr, "[fvm_stream_partitions] Unexpected slice size (%lu vs %lu)\n",
                info.slice_size, hdr.slice_size);
        return ZX_ERR_IO;
    }

    fbl::unique_ptr<uint8_t[]> metadata(new uint8_t[hdr.header_length]);
    memcpy(metadata.get(), &hdr, sizeof(hdr));

    size_t off = sizeof(hdr);
    while (off < hdr.header_length) {
        ssize_t r = read(src_fd.get(), &metadata[off], hdr.header_length - off);
        if (r < 0) {
            fprintf(stderr, "[fvm_stream_partitions] Failed to stream metadata\n");
            return ZX_ERR_IO;
        }
        off += r;
    }

    fbl::Array<partition_info> parts(new partition_info[hdr.partition_count],
                                     hdr.partition_count);

    fvm::partition_descriptor_t* part =
            reinterpret_cast<fvm::partition_descriptor_t*>(
                    reinterpret_cast<uintptr_t>(metadata.get()) +
                    sizeof(fvm::sparse_image_t));

    for (size_t p = 0; p < hdr.partition_count; p++) {
        parts[p].pd = part;
        parts[p].old_part.reset(open_partition(nullptr, part->type, ZX_SEC(2), nullptr));

        if (parts[p].pd->magic != fvm::kPartitionDescriptorMagic) {
            fprintf(stderr, "[fvm_stream_partitions] Bad partition magic\n");
            return ZX_ERR_IO;
        }

        if (parts[p].old_part) {
            bool is_vpartition;
            if (fvm_is_vpartition(parts[p].old_part.get(), &is_vpartition)) {
                fprintf(stderr, "[fvm_stream_partitions] Couldn't confirm old vpartition type\n");
                return ZX_ERR_IO;
            } else if (fvm_partition_match(fvm_fd.get(), parts[p].old_part.get()) != ZX_OK) {
                fprintf(stderr, "Streaming a partition type which also exists outside FVM\n");
                fprintf(stderr, "Please run 'install-disk-image wipe' to clear your partitions\n");
                return ZX_ERR_BAD_STATE;
            } else if (!is_vpartition) {
                fprintf(stderr, "Streaming a partition type which also exists in a GPT\n");
                fprintf(stderr, "Please run 'install-disk-image wipe' to clear your GPT.\n");
                return ZX_ERR_BAD_STATE;
            }
        }

        fvm::extent_descriptor_t* ext = get_extent(part, 0);
        if (ext->magic != fvm::kExtentDescriptorMagic) {
            fprintf(stderr, "[fvm_stream_partitions] Bad extent magic\n");
            return ZX_ERR_IO;
        } else if (ext->slice_start != 0) {
            fprintf(stderr, "[fvm_stream_partitions] First slice must start at zero\n");
            return ZX_ERR_IO;
        } else if (ext->slice_count == 0) {
            fprintf(stderr, "[fvm_stream_partitions] Extents must have > 0 slices\n");
            return ZX_ERR_IO;
        } else if (ext->extent_length > ext->slice_count * hdr.slice_size) {
            fprintf(stderr, "[fvm_stream_partitions] Extent length must fit within allocated slice count\n");
            return ZX_ERR_IO;
        }

        alloc_req_t alloc;
        // Allocate this partition as inactive so it gets deleted on the next
        // reboot if this stream fails.
        alloc.flags = fvm::kVPartFlagInactive;
        alloc.slice_count = ext->slice_count;
        memcpy(&alloc.type, parts[p].pd->type, sizeof(alloc.type));
        size_t sz;
        if (zx_cprng_draw(alloc.guid, GPT_GUID_LEN, &sz) != ZX_OK ||
            sz != GPT_GUID_LEN) {
            fprintf(stderr, "[fvm_stream_partitions] Couldn't generate unique GUID\n");
            return ZX_ERR_IO;
        }
        memcpy(&alloc.name, parts[p].pd->name, sizeof(alloc.name));
        printf("[fvm_stream_partitions] allocating partition %s consisting of %lu slices\n",
               alloc.name, alloc.slice_count);
        parts[p].new_part.reset(fvm_allocate_partition(fvm_fd.get(), &alloc));

        if (!parts[p].new_part) {
            fprintf(stderr, "[fvm_stream_partitions] Couldn't allocate partition\n");
            return ZX_ERR_BAD_STATE;
        }

        for (size_t e = 1; e < parts[p].pd->extent_count; e++) {
            ext = get_extent(parts[p].pd, e);
            if (ext->magic != fvm::kExtentDescriptorMagic) {
                fprintf(stderr, "[fvm_stream_partitions] Bad extent magic\n");
                return ZX_ERR_IO;
            } else if (ext->slice_count == 0) {
                fprintf(stderr, "[fvm_stream_partitions] Extents must have > 0 slices\n");
                return ZX_ERR_IO;
            } else if (ext->extent_length > ext->slice_count * hdr.slice_size) {
                fprintf(stderr, "[fvm_stream_partitions] Extent must fit within allocated slice count\n");
                return ZX_ERR_IO;
            }

            extend_request_t request;
            request.offset = ext->slice_start;
            request.length = ext->slice_count;
            printf("[fvm_stream_partitions] Extending partition[%lu] at offset %lu by length %lu\n",
                   p, request.offset, request.length);
            if (ioctl_block_fvm_extend(parts[p].new_part.get(), &request) < 0) {
                fprintf(stderr, "[fvm_stream_partitions] Failed to extend partition\n");
                return ZX_ERR_BAD_STATE;
            }
        }
        part = reinterpret_cast<fvm::partition_descriptor*>(
                reinterpret_cast<uintptr_t>(ext) + sizeof(fvm::extent_descriptor_t));
    }

    printf("[fvm_stream_partitions] Partition space pre-allocated\n");

    const size_t vmo_sz = 1 << 20;

    fbl::unique_ptr<MappedVmo> mvmo;
    zx_status_t status = MappedVmo::Create(vmo_sz, "fvm-stream", &mvmo);
    if (status != ZX_OK) {
        fprintf(stderr, "[fvm_stream_partitions] Failed to create stream VMO\n");
        return ZX_ERR_NO_MEMORY;
    }

    // Now that all partitions are preallocated, begin streaming data to them.
    for (size_t p = 0; p < hdr.partition_count; p++) {
        txnid_t txnid;
        vmoid_t vmoid;
        fifo_client_t* client;
        zx_status_t status = register_fast_block_io(parts[p].new_part.get(),
                                                    mvmo->GetVmo(), &txnid,
                                                    &vmoid, &client);
        if (status != ZX_OK) {
            fprintf(stderr, "[fvm_stream_partitions] Failed to register fast block IO\n");
            return status;
        }

        block_fifo_request_t request;
        request.txnid = txnid;
        request.vmoid = vmoid;
        request.opcode = BLOCKIO_WRITE;

        printf("[fvm_stream_partitions] streaming partition %lu\n", p);
        status = stream_fvm_partition(&parts[p], mvmo.get(), client,
                                      hdr.slice_size, &request, src_fd.get());
        printf("[fvm_stream_partitions] done streaming partition %lu\n", p);
        block_fifo_release_client(client);
        if (status != ZX_OK) {
            fprintf(stderr, "[fvm_stream_partitions] Failed to stream partition\n");
            return status;
        }
    }

    for (size_t p = 0; p < hdr.partition_count; p++) {
        // Upgrade the old partition (currently active) to the new partition (currently
        // inactive), so when the new partition becomes active, the old
        // partition is destroyed.
        upgrade_req_t upgrade;
        memset(&upgrade, 0, sizeof(upgrade));
        if (parts[p].old_part) {
            if (ioctl_block_get_partition_guid(parts[p].old_part.get(),
                                               &upgrade.old_guid, GUID_LEN) < 0) {
                fprintf(stderr, "[fvm_stream_partitions] Failed to get unique GUID of old partition\n");
                return ZX_ERR_BAD_STATE;
            }
        }
        if (ioctl_block_get_partition_guid(parts[p].new_part.get(), &upgrade.new_guid, GUID_LEN) < 0) {
            fprintf(stderr, "[fvm_stream_partitions] Failed to get unique GUID of new partition\n");
            return ZX_ERR_BAD_STATE;
        }

        if (ioctl_block_fvm_upgrade(fvm_fd.get(), &upgrade) < 0) {
            fprintf(stderr, "[fvm_stream_partitions] Failed to upgrade partition\n");
            return ZX_ERR_IO;
        }

        if (parts[p].old_part) {
            // This would fail if the old part was on GPT, not FVM. However,
            // we checked earlier and verified that parts[p].old_part, if it exists,
            // is a vpartition.
            ssize_t r;
            if ((r = ioctl_block_fvm_destroy(parts[p].old_part.get())) < 0) {
                fprintf(stderr, "[fvm_stream_partitions] Couldn't destroy partition: %ld\n", r);
                return static_cast<zx_status_t>(r);
            }
        }
    }

    return ZX_OK;
}

// Find and return the topological path of the GPT which we will pave.
// |out_path| must be at least |PATH_MAX| bytes long.
zx_status_t find_target_gpt(char* out_path) {
    DIR* d = opendir(kBlockDevPath);
    if (d == nullptr) {
        fprintf(stderr, "[find_target_gpt] Cannot inspect block devices\n");
        return ZX_ERR_BAD_STATE;
    }

    struct dirent* de;
    while ((de = readdir(d)) != nullptr) {
        fbl::unique_fd fd(openat(dirfd(d), de->d_name, O_RDWR));
        if (!fd) {
            continue;
        }
        ssize_t r = ioctl_device_get_topo_path(fd.get(), out_path, PATH_MAX);
        if (r < 0) {
            continue;
        }

        // TODO(ZX-1344): This is a hack, but practically, will work for our
        // usage.  The GPT which will contain an FVM should be a block device
        // that is a SATA device, but not a partition itself.
        if (strstr(out_path, "sata") != nullptr && strstr(out_path, "part") == nullptr) {
            closedir(d);
            return ZX_OK;
        }
    }
    closedir(d);

    fprintf(stderr, "[find_target_gpt] No candidate GPT found\n");
    return ZX_ERR_NOT_FOUND;
}

// Initialize a GPT object with the gpt_device_t wrapper from ulib/gpt.
zx_status_t initialize_gpt(const char* gpt_path, fbl::unique_fd* out_fd, gpt_device_t** out_gpt) {
    fbl::unique_fd fd(open(gpt_path, O_RDWR));
    if (!fd) {
        fprintf(stderr, "[initialize_gpt] Failed to open GPT\n");
        return ZX_ERR_IO;
    }
    block_info_t info;
    ssize_t rc = ioctl_block_get_info(fd.get(), &info);
    if (rc < 0) {
        fprintf(stderr, "[initialize_gpt] Couldn't get GPT block info\n");
        return ZX_ERR_IO;
    }

    if (gpt_device_init(fd.get(), info.block_size, info.block_count, out_gpt)) {
        fprintf(stderr, "[initialize_gpt] Failed to get GPT info\n");
        return ZX_ERR_IO;
    } else if (!(*out_gpt)->valid) {
        gpt_device_release(*out_gpt);
        return ZX_ERR_IO;
    }
    *out_fd = fbl::move(fd);
    return ZX_OK;
}

struct Partition {
    size_t start;  // Block, inclusive
    size_t length; // In Blocks
};

constexpr size_t kReservedEntryBlocks = (16 * 1024);
constexpr size_t kReservedHeaderBlocks(size_t blk_size) {
    return (kReservedEntryBlocks + 2 * blk_size) / blk_size;
};

// Find the first spot that has at least |bytes_requested| of space.
// Does not update the GPT.
//
// Returns the |start_out| block and |length_out| blocks, indicating
// how much space was found, on success. This may be larger than
// the number of bytes requested.
zx_status_t find_first_fit(const char* gpt_path, size_t bytes_requested,
                           size_t* start_out, size_t* length_out) {
    printf("[find_first_fit]\n");
    // Gather GPT-related information.
    fbl::unique_fd gpt_fd;
    gpt_device_t* gpt;
    zx_status_t status;
    if ((status = initialize_gpt(gpt_path, &gpt_fd, &gpt)) != ZX_OK) {
        fprintf(stderr, "[find_first_fit] Cannot initialize GPT\n");
        return status;
    }
    block_info_t info;
    ssize_t rc = ioctl_block_get_info(gpt_fd.get(), &info);
    if (rc < 0) {
        fprintf(stderr, "[find_first_fit] Cannot acquire GPT info\n");
        return static_cast<zx_status_t>(rc);
    }
    size_t blocks_requested = (bytes_requested + info.block_size - 1) / info.block_size;

    // Sort all partitions by starting block.
    // For simplicity, include the 'start' and 'end' reserved spots as
    // partitions.
    size_t partc = 0;
    Partition partitions[PARTITIONS_COUNT + 2];
    const size_t kReservedBlocks = kReservedHeaderBlocks(info.block_size);
    partitions[partc].start = 0;
    partitions[partc++].length = kReservedBlocks;
    partitions[partc].start = info.block_count - kReservedBlocks;
    partitions[partc++].length = kReservedBlocks;

    for (size_t i = 0; i < PARTITIONS_COUNT; i++) {
        gpt_partition_t* p = gpt->partitions[i];
        if (!p) {
            continue;
        }
        partitions[partc].start = p->first;
        partitions[partc].length = p->last - p->first + 1;
        printf("[find_first_fit] Partition seen with start %lu, end %lu (length %lu)\n",
               p->first, p->last, partitions[partc].length);
        partc++;
    }
    printf("[find_first_fit] Sorting\n");
    qsort(partitions, partc, sizeof(Partition), [](const void* p1, const void* p2) {
        ssize_t s1 = static_cast<ssize_t>(static_cast<const Partition*>(p1)->start);
        ssize_t s2 = static_cast<ssize_t>(static_cast<const Partition*>(p2)->start);
        return static_cast<int>(s1 - s2);
    });

    // Look for space between the partitions. Since the reserved spots of the
    // GPT were included in |partitions|, all available space will be located
    // "between" partitions.
    for (size_t i = 0; i < partc - 1; i++) {
        size_t next = partitions[i].start + partitions[i].length;
        printf("[find_first_fit] Partition[%lu] From Block [%lu, %lu) ..."
                "(next partition starts at block %lu)\n",
               i, partitions[i].start, next, partitions[i + 1].start);

        if (next > partitions[i + 1].start) {
            fprintf(stderr, "[find_first_fit] Corrupted GPT\n");
            return ZX_ERR_IO;
        }
        size_t free_blocks = partitions[i + 1].start - next;
        printf("[find_first_fit]    There are %lu free blocks (%lu requested)\n", free_blocks,
                blocks_requested);
        if (free_blocks >= blocks_requested) {
            *start_out = next;
            *length_out = free_blocks;
            return ZX_OK;
        }
    }
    fprintf(stderr, "[find_first_fit] No GPT space found\n");
    return ZX_ERR_NO_RESOURCES;
}

// Returns a file descriptor to an EFI partition which can be paved,
// allocating the EFI partition if necessary.
zx_status_t efi_find_or_add(fbl::unique_fd *efi_fd) {
    printf("[efi_find_or_add]\n");
    char gpt_path[PATH_MAX];
    if (find_target_gpt(gpt_path)) {
        return ZX_ERR_IO;
    }

    fbl::unique_fd gpt_fd;
    gpt_device_t* gpt;
    zx_status_t status;
    if ((status = initialize_gpt(gpt_path, &gpt_fd, &gpt)) != ZX_OK) {
        return status;
    }

    block_info_t info;
    ssize_t rc = ioctl_block_get_info(gpt_fd.get(), &info);
    if (rc < 0) {
        fprintf(stderr, "[efi_find_or_add] Cannot acquire GPT info\n");
        return static_cast<zx_status_t>(rc);
    }

    zx_status_t r = 0;
    uint8_t type[GPT_GUID_LEN] = GUID_EFI_VALUE;
    // Skip the first partition in the GPT; if it is EFI, we don't
    // want to overwrite it.
    for (size_t i = 1; i < PARTITIONS_COUNT; i++) {
        gpt_partition_t* p = gpt->partitions[i];
        if (!p) {
            continue;
        }

        if (memcmp(p->type, type, GPT_GUID_LEN) == 0) {
            printf("[efi_find_or_add] Found EFI partition in GPT, partition %lu\n", i);
            efi_fd->reset(open_partition(p->guid, type, ZX_SEC(5), nullptr));
            if (!*efi_fd) {
                fprintf(stderr, "[efi_find_or_add] Couldn't open EFI partition\n");
                return ZX_ERR_IO;
            }
            return ZX_OK;
        }
    }

    const size_t kMinimumEFISizeBytes = 1LU * (1 << 30);
    uint64_t start, length;
    if ((r = find_first_fit(gpt_path, kMinimumEFISizeBytes, &start, &length)) != ZX_OK) {
        fprintf(stderr, "[efi_find_or_add] Couldn't find fit\n");
        return r;
    }
    length = (kMinimumEFISizeBytes + info.block_size - 1) / info.block_size;
    size_t sz;
    uint8_t guid[GPT_GUID_LEN];
    if ((r = zx_cprng_draw(guid, GPT_GUID_LEN, &sz)) != ZX_OK) {
        fprintf(stderr, "[efi_find_or_add] Failed to get random GUID\n");
        return r;
    } else if ((r = gpt_partition_add(gpt, "EFI Gigaboot", type, guid, start, length, 0))) {
        fprintf(stderr, "[efi_find_or_add] Failed to add EFI partition\n");
        return r;
    } else if ((r = gpt_device_sync(gpt))) {
        fprintf(stderr, "[efi_find_or_add] Failed to sync GPT\n");
        return r;
    } else if ((r = (int) ioctl_block_rr_part(gpt_fd.get())) < 0) {
        fprintf(stderr, "[efi_find_or_add] Failed to rebind GPT\n");
        return r;
    }
    efi_fd->reset(open_partition(guid, type, ZX_SEC(5), nullptr));
    if (!*efi_fd) {
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

// Assuming the path to the GPT does not already contain an
// FVM, find space for an FVM partition, and add it to the GPT.
zx_status_t fvm_add_to_gpt(const char* gpt_path) {
    fbl::unique_fd gpt_fd;
    gpt_device_t* gpt;
    zx_status_t status;
    if ((status = initialize_gpt(gpt_path, &gpt_fd, &gpt)) != ZX_OK) {
        return status;
    }

    block_info_t info;
    ssize_t rc = ioctl_block_get_info(gpt_fd.get(), &info);
    if (rc < 0) {
        fprintf(stderr, "[fvm_add_to_gpt] Cannot acquire GPT info\n");
        return static_cast<zx_status_t>(rc);
    }

    int r = 0;
    const size_t kMinimumFVMSizeBytes = 8LU * (1 << 30);
    const size_t kOptionalReserveBytes = 4LU * (1 << 30);
    const size_t kOptionalReserveBlocks = kOptionalReserveBytes / info.block_size;
    size_t start = 0;
    size_t length = 0;
    uint8_t type[GPT_GUID_LEN] = GUID_FVM_VALUE;
    uint8_t guid[GPT_GUID_LEN];
    size_t sz;
    fbl::unique_fd partition_fd;
    for (size_t i = 0; i < PARTITIONS_COUNT; i++) {
        gpt_partition_t* p = gpt->partitions[i];
        if (!p) {
            continue;
        }
        // If the FVM already exists within the GPT, return early.
        if (memcmp(p->type, type, GPT_GUID_LEN) == 0) {
            printf("[fvm_add_to_gpt] FVM partition already exists within GPT\n");
            memcpy(guid, p->guid, GPT_GUID_LEN);
            goto done;
        }
    }

    if ((r = find_first_fit(gpt_path, kMinimumFVMSizeBytes, &start, &length)) != ZX_OK) {
        fprintf(stderr, "[fvm_add_to_gpt] Couldn't find space in GPT: %d\n", r);
        goto done;
    }
    printf("[fvm_add_to_gpt] Found space in GPT - OK %lu @ %lu\n", length, start);

    // If can fulfill the requested size, and we still have space for the
    // optional reserve section, then we should shorten the amount of blocks
    // we're asking for.
    //
    // This isn't necessary, but it allows growing the GPT later, if necessary.
    if (length - kOptionalReserveBlocks > (kMinimumFVMSizeBytes / info.block_size)) {
        printf("[fvm_add_to_gpt] Space for reserve - OK\n");
        length -= kOptionalReserveBlocks;
    }
    printf("[fvm_add_to_gpt] Final space in GPT - OK %lu @ %lu\n", length, start);

    if ((r = zx_cprng_draw(guid, GPT_GUID_LEN, &sz)) != ZX_OK) {
        fprintf(stderr, "[fvm_add_to_gpt] Failed to get random GUID\n");
        goto done;
    } else if ((r = gpt_partition_add(gpt, "fvm", type, guid, start, length, 0))) {
        fprintf(stderr, "[fvm_add_to_gpt] Failed to add FVM partition\n");
        goto done;
    } else if ((r = gpt_device_sync(gpt))) {
        fprintf(stderr, "[fvm_add_to_gpt] Failed to sync GPT\n");
        goto done;
    } else if ((r = (int) ioctl_block_rr_part(gpt_fd.get())) < 0) {
        fprintf(stderr, "[fvm_add_to_gpt] Failed to rebind GPT\n");
        goto done;
    }

    printf("[fvm_add_to_gpt] Added partition, waiting for bind\n");
done:
    if (r == 0) {
        // Before we return, claiming that the FVM partition is ready, we should
        // check the GPT partition has actually appeared in devfs.
        partition_fd.reset(open_partition(guid, type, ZX_SEC(5), nullptr));
        if (!partition_fd) {
            fprintf(stderr, "[fvm_add_to_gpt] Added partition, waiting for bind - NOT FOUND\n");
            r = -1;
        } else {
            printf("[fvm_add_to_gpt] Added partition, waiting for bind - OK\n");
            r = 0;
        }
    }
    gpt_device_release(gpt);
    return (r < 0 ? ZX_ERR_BAD_STATE : ZX_OK);
}

} // namespace

// Paves a sparse_file to the underlying disk, on top
// of a GPT.
int fvm_pave(fbl::unique_fd fd) {
    printf("[fvm_pave]\n");
    char gpt_path[PATH_MAX];
    if (find_target_gpt(gpt_path)) {
        fprintf(stderr, "[fvm_pave] Couldn't find target GPT\n");
        return -1;
    }
    printf("[fvm_pave] Found Target GPT %s - OK\n", gpt_path);
    if (fvm_add_to_gpt(gpt_path)) {
        fprintf(stderr, "[fvm_pave] Couldn't format FVM partition\n");
        return -1;
    }
    printf("[fvm_pave] Added to GPT - OK\n");

    printf("[fvm_pave] Streaming partitions...\n");
    zx_status_t status = fvm_stream_partitions(fbl::move(fd));
    if (status != ZX_OK) {
        fprintf(stderr, "[fvm_pave] Failed to stream partitions: %d\n", status);
        return -1;
    }
    printf("[fvm_pave] DONE\n");
    return 0;
}

// Paves an EFI image onto disk, within the GPT.
int efi_pave(fbl::unique_fd fd) {
    printf("[efi_pave]\n");
    fbl::unique_fd efi_fd;
    if (efi_find_or_add(&efi_fd) != ZX_OK) {
        fprintf(stderr, "efi_pave: Cannot find suitable EFI partition (or cannot make one)\n");
        return -1;
    }
    printf("[efi_pave] Found or Added EFI - OK\n");

    block_info_t info;
    if (ioctl_block_get_info(efi_fd.get(), &info) < 0) {
        fprintf(stderr, "[efi_pave] Couldn't get GPT block info\n");
        return -1;
    }

    const size_t vmo_sz = 1 << 20;
    fbl::unique_ptr<MappedVmo> mvmo;
    zx_status_t status = MappedVmo::Create(vmo_sz, "efi-pave", &mvmo);
    if (status != ZX_OK) {
        fprintf(stderr, "[efi_pave] Failed to create stream VMO\n");
        return -1;
    }

    txnid_t txnid;
    vmoid_t vmoid;
    fifo_client_t* client;
    status = register_fast_block_io(efi_fd.get(), mvmo->GetVmo(), &txnid,
                                    &vmoid, &client);
    if (status != ZX_OK) {
        fprintf(stderr, "[efi_pave] Cannot register fast block I/O\n");
        return -1;
    }

    block_fifo_request_t request;
    request.txnid = txnid;
    request.vmoid = vmoid;
    request.opcode = BLOCKIO_WRITE;
    status = stream_partition(mvmo.get(), client, &request, fd.get());

    block_fifo_release_client(client);
    if (status != ZX_OK) {
        return -1;
    }
    printf("[efi_pave] Completed successfully\n");

    return 0;
}

// Wipes the following partitions:
// - System
// - Data
// - Blobstore
// - FVM
//
// From the target GPT, leaving it (hopefully) in a state
// ready for a sparse FVM image to be installed.
int fvm_clean() {
    char gpt_path[PATH_MAX];
    if (find_target_gpt(gpt_path)) {
        return -1;
    }

    fbl::unique_fd fd;
    gpt_device_t* gpt;
    if (initialize_gpt(gpt_path, &fd, &gpt)) {
        return -1;
    }

    bool modify = false;
    for (size_t i = 0; i < PARTITIONS_COUNT; i++) {
        if (!gpt->partitions[i]) {
            continue;
        }
        const uint8_t system_type[GPT_GUID_LEN] = GUID_SYSTEM_VALUE;
        const uint8_t data_type[GPT_GUID_LEN] = GUID_DATA_VALUE;
        const uint8_t blobfs_type[GPT_GUID_LEN] = GUID_BLOBFS_VALUE;
        const uint8_t fvm_type[GPT_GUID_LEN] = GUID_FVM_VALUE;
        if (!memcmp(gpt->partitions[i]->type, system_type, GPT_GUID_LEN)) {
            printf("Removing system partition\n");
        } else if (!memcmp(gpt->partitions[i]->type, data_type, GPT_GUID_LEN)) {
            printf("Removing data partition\n");
        } else if (!memcmp(gpt->partitions[i]->type, blobfs_type, GPT_GUID_LEN)) {
            printf("Removing blobstore partition\n");
        } else if (!memcmp(gpt->partitions[i]->type, fvm_type, GPT_GUID_LEN)) {
            printf("Removing fvm partition\n");
        } else {
            continue;
        }
        modify = true;

        // Overwrite the first 4k to (hackily) ensure the destroyed partition
        // doesn't "reappear" in place.
        char buf[4192];
        memset(buf, 0, sizeof(buf));
        fbl::unique_fd pfd(open_partition(gpt->partitions[i]->guid,
                                          gpt->partitions[i]->type, ZX_SEC(2),
                                          nullptr));
        write(pfd.get(), buf, sizeof(buf));
        gpt_partition_remove(gpt, gpt->partitions[i]->guid);
    }
    if (modify) {
        gpt_device_sync(gpt);
        printf("GPT updated, reboot strongly recommended immediately\n");
    }
    gpt_device_release(gpt);
    ioctl_block_rr_part(fd.get());
    return 0;
}

int usage() {
    fprintf(stderr, "install-disk-image [command] <options*>\n");
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  install-fvm : Install a sparse FVM to the device\n");
    fprintf(stderr, "  install-efi : Install an EFI partition to the device\n");
    fprintf(stderr, "  wipe        : Clean up the install disk\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --file <file>: Read from FILE instead of stdin\n");
    return -1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "install-disk-image needs a command\n");
        return usage();
    }
    argc--;
    argv++;
    char* cmd = argv[0];

    argc--;
    argv++;

    fbl::unique_fd fd(STDIN_FILENO);
    while (argc > 0) {
        if (!strcmp(argv[0], "--file")) {
            argc--;
            argv++;
            if (argc < 1) {
                fprintf(stderr, "'--file' argument requires a file\n");
                return -1;
            }
            fd.reset(open(argv[0], O_RDONLY));
            if (!fd) {
                fprintf(stderr, "Couldn't open supplied file\n");
                return -1;
            }
            argc--;
            argv++;
        } else {
            return usage();
        }
    }

    if (!strcmp(cmd, "install-efi")) {
        return efi_pave(fbl::move(fd));
    } else if (!strcmp(cmd, "install-fvm")) {
        return fvm_pave(fbl::move(fd));
    } else if (!strcmp(cmd, "wipe")) {
        return fvm_clean();
    }
    return usage();
}
