// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flash_stress.h"

#include <fuchsia/hardware/block/cpp/fidl.h>
#include <fuchsia/hardware/block/volume/cpp/fidl.h>
#include <inttypes.h>
#include <lib/fdio/directory.h>
#include <lib/zx/clock.h>
#include <lib/zx/fifo.h>
#include <lib/zx/time.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
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
constexpr uint32_t kMinFvmFreeSpace = 100 * 1024 * 1024;
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

void VerifySectorData(zx_vaddr_t start, uint64_t value) {
  uint64_t num_words = kSectorSize / sizeof(value);
  uint64_t* data = reinterpret_cast<uint64_t*>(start);
  for (uint64_t i = 0; i < num_words; i++) {
    if (unlikely(data[i] != value)) {
      ZX_PANIC("Found error: expected 0x%016" PRIX64 ", got 0x%016" PRIX64 " at offset %" PRIu64
               "\n",
               value, data[i], value * kSectorSize + i * sizeof(value));
    }
  }
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

zx_status_t FlashIo(const BlockDevice& device, size_t bytes_to_test, bool is_write_test) {
  size_t vmo_byte_offset = 0;
  size_t bytes_per_request =
      kTransferSize > device.info.max_transfer_size ? device.info.max_transfer_size : kTransferSize;

  size_t count = bytes_to_test / bytes_per_request;

  size_t num_sectors = kTransferSize / kSectorSize;
  size_t blksize = device.info.block_size;

  size_t dev_off = 0;
  uint64_t word = 0;
  reqid_t next_reqid = 0;

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

std::unique_ptr<TemporaryFvmPartition> TemporaryFvmPartition::Create(int fvm_fd,
                                                                     uint64_t slices_requested) {
  uuid::Uuid unique_guid = uuid::Uuid::Generate();

  alloc_req_t request{.slice_count = slices_requested,
                      .name = "flash-test-fs",
                      .flags = fuchsia::hardware::block::volume::ALLOCATE_PARTITION_FLAG_INACTIVE};
  memcpy(request.guid, unique_guid.bytes(), sizeof(request.guid));
  memcpy(request.type, kTestPartGUID.bytes(), sizeof(request.type));

  // Create a new partition.
  fbl::unique_fd fd(fvm_allocate_partition(fvm_fd, &request));
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
      new TemporaryFvmPartition(partition_path, unique_guid));
}

TemporaryFvmPartition::TemporaryFvmPartition(std::string partition_path, uuid::Uuid unique_guid)
    : partition_path_(partition_path), unique_guid_(unique_guid) {}

TemporaryFvmPartition::~TemporaryFvmPartition() {
  ZX_ASSERT(destroy_partition(unique_guid_.bytes(), kTestPartGUID.bytes()) == ZX_OK);
}

std::string TemporaryFvmPartition::GetPartitionPath() { return partition_path_; }

// Start a stress test.
bool StressFlash(StatusLine* status, const CommandLineArgs& args, zx::duration duration) {
  // Access the FVM.
  fbl::unique_fd fvm_fd(open(args.fvm_path.c_str(), O_RDWR));
  if (!fvm_fd) {
    status->Log("Error: Could not open FVM\n");
    return false;
  }

  // Calculate available space and number of slices needed.
  fuchsia_hardware_block_volume_VolumeInfo info;
  if (fvm_query(fvm_fd.get(), &info) != ZX_OK) {
    status->Log("Error: Could not get FVM info\n");
    return false;
  }

  // Default to using all available disk space.
  uint64_t slices_available = info.pslice_total_count - info.pslice_allocated_count;
  uint64_t bytes_to_test = slices_available * info.slice_size -
                           RoundUp(kMinFvmFreeSpace, info.slice_size) - kMinPartitionFreeSpace;
  // If a value was specified and does not exceed the free disk space, use that.
  if (args.mem_to_test_megabytes.has_value()) {
    uint64_t bytes_requested = args.mem_to_test_megabytes.value() * 1024 * 1024;
    if (bytes_requested <= bytes_to_test) {
      bytes_to_test = bytes_requested;
    } else {
      status->Log("Specified disk size (%ld bytes) exceeds available disk size (%ld bytes).\n",
                  bytes_requested, bytes_to_test);
      return false;
    }
  }
  uint64_t slices_requested = RoundUp(bytes_to_test, info.slice_size) / info.slice_size;

  std::unique_ptr<TemporaryFvmPartition> fvm_partition =
      TemporaryFvmPartition::Create(fvm_fd.get(), slices_requested);

  if (fvm_partition == nullptr) {
    status->Log("Failed to create FVM partition");
    return false;
  }

  std::string partition_path = fvm_partition->GetPartitionPath();

  uint64_t vmo_size = info.slice_size;

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

  zx::time end_time = zx::deadline_after(duration);

  do {
    if (FlashIo(device, bytes_to_test, /*is_write_test=*/true) != ZX_OK) {
      status->Log("Error writing to vmo.");
      return false;
    }

    if (FlashIo(device, bytes_to_test, /*is_write_test=*/false) != ZX_OK) {
      status->Log("Error reading from vmo.");
      return false;
    }
  } while (zx::clock::get_monotonic() < end_time);

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
