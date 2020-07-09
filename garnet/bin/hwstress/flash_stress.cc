// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flash_stress.h"

#include <fuchsia/hardware/block/cpp/fidl.h>
#include <fuchsia/hardware/block/volume/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/fifo.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/status.h>

#include <string>

#include <fbl/unique_fd.h>
#include <fs-management/fvm.h>
#include <src/lib/uuid/uuid.h>

#include "status.h"
#include "util.h"

namespace hwstress {

namespace {

constexpr uint32_t kSectorSize = 512;
constexpr uint32_t kTransferSize = 512 * 1024;
constexpr uint32_t kMinFvmFreeSpace = 16 * 1024 * 1024;
constexpr uint32_t kMinPartitionFreeSpace = 2 * 1024 * 1024;

struct BlockDevice {
  fuchsia::hardware::block::BlockSyncPtr device;  // Connection to the block device.
  zx::fifo fifo;                                  // FIFO used to read/write to the block device.
  fuchsia::hardware::block::BlockInfo info;       // Details about the block device.
  zx::vmo vmo;                                    // Shared VMO with the block device.
  zx_vaddr_t vmo_addr;                            // Where |vmo| is mapped into our address space.
  size_t vmo_size;                                // Size of |vmo| in bytes.
  fuchsia::hardware::block::VmoId vmoid;          // Identifier the used to refer to the VMO
                                                  // when communicating with the block device.
};

void WriteSectorData(zx_vaddr_t start, uint64_t value) {
  uint64_t num_words = kSectorSize / sizeof(value);
  uint64_t* data = reinterpret_cast<uint64_t*>(start);
  for (uint64_t i = 0; i < num_words; i++) {
    data[i] = value;
  }
}

uint64_t VerifySectorData(zx_vaddr_t start, uint64_t value) {
  uint64_t num_words = kSectorSize / sizeof(value);
  uint64_t* data = reinterpret_cast<uint64_t*>(start);
  uint64_t errors_detected = 0;
  for (uint64_t i = 0; i < num_words; i++) {
    errors_detected += (data[i] != value);
  }
  return errors_detected;
}

zx_status_t OpenBlockDevice(const char* path, size_t vmo_size, BlockDevice* block_device) {
  BlockDevice result{};
  result.vmo_size = vmo_size;

  // Create a channel, and connect to block device.
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_service_connect(path, server.release());
  if (status != ZX_OK) {
    return status;
  }
  result.device.Bind(std::move(client));

  // Fetch information about the underlying block device, such as block size.
  std::unique_ptr<fuchsia::hardware::block::BlockInfo> info;
  zx_status_t io_status = result.device->GetInfo(&status, &info);
  if (io_status != ZX_OK || status != ZX_OK) {
    fprintf(stderr, "error: cannot get block device info for '%s'\n", path);
    return ZX_ERR_INTERNAL;
  }
  result.info = *info;

  // Fetch a FIFO for communicating with the block device over.
  io_status = result.device->GetFifo(&status, &result.fifo);
  if (io_status != ZX_OK || status != ZX_OK) {
    fprintf(stderr, "error: cannot get fifo for '%s'\n", path);
    return ZX_ERR_INTERNAL;
  }

  // Setup a shared VMO with the block device.
  status = zx::vmo::create(vmo_size, /*options=*/0, &result.vmo);
  if (status != ZX_OK) {
    fprintf(stderr, "error: could not allocate memory: %s\n", zx_status_get_string(status));
    return status;
  }
  zx::vmo shared_vmo;
  status = result.vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &shared_vmo);
  if (status != ZX_OK) {
    fprintf(stderr, "error: cannot duplicate handle: %s\n", zx_status_get_string(status));
    return status;
  }
  std::unique_ptr<fuchsia::hardware::block::VmoId> vmo_id;
  io_status = result.device->AttachVmo(std::move(shared_vmo), &status, &vmo_id);
  if (io_status != ZX_OK || status != ZX_OK || vmo_id == nullptr) {
    fprintf(stderr, "error: cannot attach vmo for '%s'\n", path);
    return ZX_ERR_INTERNAL;
  }
  result.vmoid = *vmo_id;
  *block_device = std::move(result);
  return ZX_OK;
}

zx_status_t SendCommandBlocking(const zx::fifo& fifo, const block_fifo_request_t& request) {
  zx_status_t r;
  while (true) {
    r = fifo.write(sizeof(request), &request, 1, NULL);
    if (r == ZX_OK) {
      break;
    }
    if (r != ZX_ERR_SHOULD_WAIT) {
      fprintf(stderr, "error: failed writing fifo: %s\n", zx_status_get_string(r));
      return r;
    }
    r = fifo.wait_one(ZX_FIFO_WRITABLE | ZX_FIFO_PEER_CLOSED, zx::time(ZX_TIME_INFINITE), NULL);
    if (r != ZX_OK) {
      fprintf(stderr, "failed waiting for fifo: %s\n", zx_status_get_string(r));
      return r;
    }
  }

  block_fifo_response_t resp;
  while (true) {
    r = fifo.read(sizeof(resp), &resp, 1, NULL);
    if (r == ZX_OK) {
      if (resp.status == ZX_OK) {
        break;
      }
      fprintf(stderr, "error: io txn failed: %s\n", zx_status_get_string(resp.status));
      return resp.status;
    }
    if (r != ZX_ERR_SHOULD_WAIT) {
      fprintf(stderr, "error: failed reading fifo: %s\n", zx_status_get_string(r));
      return r;
    }
    r = fifo.wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED, zx::time(ZX_TIME_INFINITE), NULL);
    if (r != ZX_OK) {
      fprintf(stderr, "failed waiting for fifo: %s\n", zx_status_get_string(r));
      return r;
    }
  }
  return ZX_OK;
}

zx_status_t FlashIo(const BlockDevice& device, size_t bytes_to_test, bool is_write_test,
                    uint64_t* errors_detected) {
  size_t vmo_byte_offset = 0;
  size_t bytes_per_request =
      kTransferSize > device.info.max_transfer_size ? device.info.max_transfer_size : kTransferSize;

  size_t count = bytes_to_test / bytes_per_request;

  size_t num_sectors = kTransferSize / kSectorSize;
  size_t blksize = device.info.block_size;

  size_t dev_off = 0;
  uint64_t word = 0x01;
  reqid_t next_reqid = 0;

  *errors_detected = 0;

  while (count > 0) {
    if (is_write_test) {
      for (size_t i = 0; i < num_sectors; i++) {
        WriteSectorData(device.vmo_addr + vmo_byte_offset + kSectorSize * i, word++);
      }
    }
    block_fifo_request_t req = {};
    req.reqid = next_reqid++;
    req.vmoid = device.vmoid.id;
    req.opcode = is_write_test ? BLOCKIO_WRITE : BLOCKIO_READ;

    // |length|, |vmo_offset|, and |dev_offset| are measured in blocks.
    req.length = static_cast<uint32_t>(bytes_per_request / blksize);
    req.vmo_offset = vmo_byte_offset / blksize;
    req.dev_offset = dev_off / blksize;

    if (zx_status_t r = SendCommandBlocking(device.fifo, req); r != ZX_OK) {
      return r;
    }

    if (!is_write_test) {
      for (size_t i = 0; i < num_sectors; i++) {
        *errors_detected +=
            VerifySectorData(device.vmo_addr + vmo_byte_offset + kSectorSize * i, word++);
      }
    }

    dev_off += bytes_per_request;
    vmo_byte_offset += bytes_per_request;
    if ((vmo_byte_offset + bytes_per_request) > device.vmo_size) {
      vmo_byte_offset = 0;
    }

    count--;
  }

  return ZX_OK;
}

}  // namespace

std::unique_ptr<TemporaryFvmPartition> TemporaryFvmPartition::Create(const std::string& fvm_path,
                                                                     uint64_t bytes_requested) {
  // Access FVM
  fbl::unique_fd fvm_fd(open(fvm_path.c_str(), O_RDWR));
  if (!fvm_fd) {
    fprintf(stderr, "Error: Could not open FVM\n");
    return nullptr;
  }

  // Calculate available space and number of slices needed.
  fuchsia_hardware_block_volume_VolumeInfo info;
  if (fvm_query(fvm_fd.get(), &info) != ZX_OK) {
    fprintf(stderr, "Error: Could not get FVM info\n");
    return nullptr;
  }

  uint64_t num_slices =
      RoundUp(bytes_requested + kMinPartitionFreeSpace, info.slice_size) / info.slice_size;
  uint64_t slices_available = info.pslice_total_count - info.pslice_allocated_count;
  if (num_slices > slices_available) {
    fprintf(stderr, "Error: %ld slices needed but %ld slices available", num_slices,
            slices_available);
    num_slices = slices_available - RoundUp(kMinFvmFreeSpace, info.slice_size) / info.slice_size;
  }
  uint64_t partition_size = num_slices * info.slice_size;

  uuid::Uuid unique_guid = uuid::Uuid::Generate();

  alloc_req_t request{.slice_count = num_slices,
                      .name = "flash-test-fs",
                      .flags = fuchsia::hardware::block::volume::ALLOCATE_PARTITION_FLAG_INACTIVE};
  memcpy(request.guid, unique_guid.bytes(), sizeof(request.guid));
  memcpy(request.type, kTestPartGUID.bytes(), sizeof(request.type));

  // Create a new partition.
  fbl::unique_fd fd(fvm_allocate_partition(fvm_fd.get(), &request));
  if (!fd) {
    fprintf(stderr, "Error: Could not allocate and open FVM partition\n");
    return nullptr;
  }

  char partition_path[PATH_MAX];
  fd.reset(open_partition(unique_guid.bytes(), kTestPartGUID.bytes(), 0, partition_path));
  if (!fd) {
    destroy_partition(unique_guid.bytes(), kTestPartGUID.bytes());
    fprintf(stderr, "Could not locate FVM partition\n");
    return nullptr;
  }

  return std::unique_ptr<TemporaryFvmPartition>(
      new TemporaryFvmPartition(partition_path, partition_size, info.slice_size, unique_guid));
}

TemporaryFvmPartition::TemporaryFvmPartition(std::string partition_path, uint64_t partition_size,
                                             uint64_t slice_size, uuid::Uuid unique_guid)
    : partition_path_(partition_path),
      partition_size_(partition_size),
      slice_size_(slice_size),
      unique_guid_(unique_guid) {}

TemporaryFvmPartition::~TemporaryFvmPartition() {
  ZX_ASSERT(destroy_partition(unique_guid_.bytes(), kTestPartGUID.bytes()) == ZX_OK);
}

std::string TemporaryFvmPartition::GetPartitionPath() { return partition_path_; }

uint64_t TemporaryFvmPartition::GetPartitionSize() { return partition_size_; }

uint64_t TemporaryFvmPartition::GetSliceSize() { return slice_size_; }

// Start a stress test.
bool StressFlash(StatusLine* status, const std::string& fvm_path, uint64_t bytes_to_test) {
  std::unique_ptr<TemporaryFvmPartition> fvm_partition =
      TemporaryFvmPartition::Create(fvm_path, bytes_to_test);

  if (fvm_partition == nullptr) {
    status->Log("Failed to create FVM partition");
    return false;
  }

  std::string partition_path = fvm_partition->GetPartitionPath();
  if (bytes_to_test > (fvm_partition->GetPartitionSize() - kMinPartitionFreeSpace)) {
    bytes_to_test = fvm_partition->GetPartitionSize() - kMinPartitionFreeSpace;
  }

  uint64_t vmo_size = fvm_partition->GetSliceSize();

  BlockDevice device;
  if (OpenBlockDevice(partition_path.c_str(), vmo_size, &device) != ZX_OK) {
    status->Log("Error: Block device could not be opened");
    return false;
  }

  // Map the VMO into memory.
  zx_status_t rstatus = zx::vmar::root_self()->map(
      /*vmar_offset=*/0, device.vmo, /*vmo_offset=*/0, /*len=*/vmo_size,
      /*options=*/(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE), &device.vmo_addr);
  if (rstatus != ZX_OK) {
    status->Log("Error: VMO could not be mapped into memory");
    return false;
  }

  uint64_t errors_detected;

  if (FlashIo(device, bytes_to_test, /*is_write_test=*/true, &errors_detected) != ZX_OK) {
    status->Log("Error writing to vmo.");
    return false;
  }

  if (FlashIo(device, bytes_to_test, /*is_write_test=*/false, &errors_detected) != ZX_OK) {
    status->Log("Error reading from vmo.");
    return false;
  }
  if (errors_detected > 0) {
    status->Log("Found %lu errors.", errors_detected);
    return false;
  }

  return true;
}

void DestroyFlashTestPartitions(StatusLine* status) {
  uint32_t count = 0;
  // Remove any partitions from previous tests
  while (destroy_partition(nullptr, kTestPartGUID.bytes()) == ZX_OK) {
    count++;
  }
  status->Log("Deleted %u partitions", count);
}

}  // namespace hwstress
