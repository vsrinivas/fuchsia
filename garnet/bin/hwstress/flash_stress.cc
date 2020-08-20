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

#include <queue>
#include <string>
#include <utility>

#include <fbl/unique_fd.h>
#include <fs-management/fvm.h>
#include <src/lib/uuid/uuid.h>

#include "status.h"
#include "util.h"

namespace hwstress {

namespace {

constexpr uint32_t kMaxInFlightRequests = 8;
constexpr uint32_t kDefaultTransferSize = 1024 * 1024;
constexpr uint32_t kMinFvmFreeSpace = 16 * 1024 * 1024;
constexpr uint32_t kMinPartitionFreeSpace = 2 * 1024 * 1024;

void WriteBlockData(zx_vaddr_t start, uint32_t block_size, uint64_t value) {
  uint64_t num_words = block_size / sizeof(value);
  uint64_t* data = reinterpret_cast<uint64_t*>(start);
  for (uint64_t i = 0; i < num_words; i++) {
    data[i] = value;
  }
}

void VerifyBlockData(zx_vaddr_t start, uint32_t block_size, uint64_t value) {
  uint64_t num_words = block_size / sizeof(value);
  uint64_t* data = reinterpret_cast<uint64_t*>(start);
  for (uint64_t i = 0; i < num_words; i++) {
    if (unlikely(data[i] != value)) {
      ZX_PANIC("Found error: expected 0x%016" PRIX64 ", got 0x%016" PRIX64 " at offset %" PRIu64
               "\n",
               value, data[i], value * block_size + i * sizeof(value));
    }
  }
}

zx_status_t OpenBlockDevice(const std::string& path,
                            fuchsia::hardware::block::BlockSyncPtr* device) {
  // Create a channel, and connect to block device.
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_service_connect(path.c_str(), server.release());
  if (status != ZX_OK) {
    return status;
  }
  device->Bind(std::move(client));
  return ZX_OK;
}

zx_status_t SendFifoRequest(const zx::fifo& fifo, const block_fifo_request_t& request) {
  zx_status_t r = fifo.write(sizeof(request), &request, 1, nullptr);
  if (r == ZX_OK || r == ZX_ERR_SHOULD_WAIT) {
    return r;
  }
  fprintf(stderr, "error: failed writing fifo: %s\n", zx_status_get_string(r));
  return r;
}

zx_status_t ReceiveFifoResponse(const zx::fifo& fifo, block_fifo_response_t* resp) {
  zx_status_t r = fifo.read(sizeof(*resp), resp, 1, nullptr);
  if (r == ZX_ERR_SHOULD_WAIT) {
    // Nothing ready yet.
    return r;
  }
  if (r != ZX_OK) {
    // Transport error.
    fprintf(stderr, "error: failed reading fifo: %s\n", zx_status_get_string(r));
    return r;
  }
  if (resp->status != ZX_OK) {
    // Block device error.
    fprintf(stderr, "error: io txn failed: %s\n", zx_status_get_string(resp->status));
    return resp->status;
  }
  return ZX_OK;
}

}  // namespace

zx_status_t FlashIo(const BlockDevice& device, size_t bytes_to_test, size_t transfer_size,
                    bool is_write_test) {
  ZX_ASSERT(bytes_to_test % device.info.block_size == 0);
  size_t bytes_to_send = bytes_to_test;
  size_t bytes_to_receive = bytes_to_test;

  size_t blksize = device.info.block_size;
  size_t vmo_byte_offset = 0;
  size_t dev_off = 0;
  uint32_t opcode = is_write_test ? BLOCKIO_WRITE : BLOCKIO_READ;

  std::queue<reqid_t> ready_to_send;
  block_fifo_request_t reqs[kMaxInFlightRequests];

  for (reqid_t next_reqid = 0; next_reqid < kMaxInFlightRequests; next_reqid++) {
    reqs[next_reqid] = {.opcode = opcode,
                        .reqid = next_reqid,
                        .vmoid = device.vmoid.id,
                        // |length|, |vmo_offset|, and |dev_offset| are measured in blocks.
                        .length = static_cast<uint32_t>(transfer_size / blksize),
                        .vmo_offset = vmo_byte_offset / blksize};
    ready_to_send.push(next_reqid);
    vmo_byte_offset += transfer_size;
  }

  while (bytes_to_receive > 0) {
    // Ensure we are ready to either write to or read from the FIFO.
    zx_signals_t flags = ZX_FIFO_PEER_CLOSED;
    if (!ready_to_send.empty() && bytes_to_send > 0) {
      flags |= ZX_FIFO_WRITABLE;
    }
    if (ready_to_send.size() < kMaxInFlightRequests) {
      flags |= ZX_FIFO_READABLE;
    }
    zx_signals_t pending_signals;
    device.fifo.wait_one(flags, zx::time(ZX_TIME_INFINITE), &pending_signals);

    // If we lost our connection to the block device, abort the test.
    if ((pending_signals & ZX_FIFO_PEER_CLOSED) != 0) {
      fprintf(stderr, "Error: connection to block device lost\n");
      return ZX_ERR_PEER_CLOSED;
    }

    // If the FIFO is writable send a request unless we have kMaxInFlightRequests in flight,
    // or have finished reading/writing.
    if ((pending_signals & ZX_FIFO_WRITABLE) != 0 && !ready_to_send.empty() && bytes_to_send > 0) {
      reqid_t reqid = ready_to_send.front();
      reqs[reqid].dev_offset = dev_off / blksize;
      reqs[reqid].length = std::min(transfer_size, bytes_to_send) / blksize;
      if (is_write_test) {
        vmo_byte_offset = reqs[reqid].vmo_offset * blksize;
        for (size_t i = 0; i < reqs[reqid].length; i++) {
          uint64_t value = reqs[reqid].dev_offset + i;
          WriteBlockData(device.vmo_addr + vmo_byte_offset + blksize * i, blksize, value);
        }
      }
      zx_status_t r = SendFifoRequest(device.fifo, reqs[reqid]);
      if (r != ZX_OK) {
        return r;
      }
      dev_off += transfer_size;
      ready_to_send.pop();
      bytes_to_send -= reqs[reqid].length * blksize;
      continue;
    }

    // Process response from the block device if the FIFO is readable.
    if ((pending_signals & ZX_FIFO_READABLE) != 0) {
      ZX_ASSERT(ready_to_send.size() < kMaxInFlightRequests);
      block_fifo_response_t resp;
      zx_status_t r = ReceiveFifoResponse(device.fifo, &resp);
      if (r != ZX_OK) {
        return r;
      }

      reqid_t reqid = resp.reqid;
      bytes_to_receive -= reqs[reqid].length * blksize;
      if (!is_write_test) {
        vmo_byte_offset = reqs[reqid].vmo_offset * blksize;
        for (size_t i = 0; i < reqs[reqid].length; i++) {
          uint64_t value = reqs[reqid].dev_offset + i;
          VerifyBlockData(device.vmo_addr + vmo_byte_offset + blksize * i, blksize, value);
        }
      }
      if (bytes_to_send > 0) {
        ready_to_send.push(reqid);
      }
      continue;
    }
  }

  return ZX_OK;
}

zx_status_t SetupBlockFifo(const std::string& path, BlockDevice* device) {
  zx_status_t status;

  // Fetch a FIFO for communicating with the block device over.
  zx::fifo fifo;
  zx_status_t io_status = device->device->GetFifo(&status, &fifo);
  if (io_status != ZX_OK || status != ZX_OK) {
    fprintf(stderr, "Error: cannot get FIFO for '%s'\n", path.c_str());
    return ZX_ERR_INTERNAL;
  }

  // Setup a shared VMO with the block device.
  zx::vmo vmo;
  status = zx::vmo::create(device->vmo_size, /*options=*/0, &vmo);
  if (status != ZX_OK) {
    fprintf(stderr, "Error: could not allocate memory: %s\n", zx_status_get_string(status));
    return status;
  }
  zx::vmo shared_vmo;
  status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &shared_vmo);
  if (status != ZX_OK) {
    fprintf(stderr, "Error: cannot duplicate handle: %s\n", zx_status_get_string(status));
    return status;
  }
  std::unique_ptr<fuchsia::hardware::block::VmoId> vmo_id;
  io_status = device->device->AttachVmo(std::move(shared_vmo), &status, &vmo_id);
  if (io_status != ZX_OK || status != ZX_OK || vmo_id == nullptr) {
    fprintf(stderr, "Error: cannot attach VMO for '%s'\n", path.c_str());
    return ZX_ERR_INTERNAL;
  }
  device->vmoid = *vmo_id;

  // Map the VMO into memory.
  status = zx::vmar::root_self()->map(
      /*vmar_offset=*/0, vmo, /*vmo_offset=*/0, /*len=*/device->vmo_size,
      /*options=*/(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE), &device->vmo_addr);
  if (status != ZX_OK) {
    fprintf(stderr, "Error: VMO could not be mapped into memory: %s", zx_status_get_string(status));
    return status;
  }

  device->fifo = std::move(fifo);
  device->vmo = std::move(vmo);
  return ZX_OK;
}

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
    : partition_path_(std::move(partition_path)), unique_guid_(unique_guid) {}

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
  fuchsia_hardware_block_volume_VolumeInfo fvm_info;
  if (fvm_query(fvm_fd.get(), &fvm_info) != ZX_OK) {
    status->Log("Error: Could not get FVM info\n");
    return false;
  }

  // Default to using all available disk space.
  uint64_t slices_available = fvm_info.pslice_total_count - fvm_info.pslice_allocated_count;
  uint64_t bytes_to_test = slices_available * fvm_info.slice_size -
                           RoundUp(kMinFvmFreeSpace, fvm_info.slice_size) - kMinPartitionFreeSpace;

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
  uint64_t slices_requested = RoundUp(bytes_to_test, fvm_info.slice_size) / fvm_info.slice_size;

  std::unique_ptr<TemporaryFvmPartition> fvm_partition =
      TemporaryFvmPartition::Create(fvm_fd.get(), slices_requested);

  if (fvm_partition == nullptr) {
    status->Log("Failed to create FVM partition");
    return false;
  }

  std::string partition_path = fvm_partition->GetPartitionPath();

  BlockDevice device;
  if (OpenBlockDevice(partition_path, &device.device) != ZX_OK) {
    status->Log("Error: Block device could not be opened");
    return false;
  }

  // Fetch information about the underlying block device, such as block size.
  zx_status_t info_status;
  std::unique_ptr<fuchsia::hardware::block::BlockInfo> block_info;
  zx_status_t io_status = device.device->GetInfo(&info_status, &block_info);
  if (io_status != ZX_OK || info_status != ZX_OK) {
    status->Log("Error: cannot get block device info for '%s'\n", partition_path.c_str());
    return ZX_ERR_INTERNAL;
  }
  device.info = *block_info;

  size_t actual_transfer_size = RoundDown(
      std::min(kDefaultTransferSize, device.info.max_transfer_size), device.info.block_size);
  device.vmo_size = actual_transfer_size * kMaxInFlightRequests;

  if (SetupBlockFifo(partition_path, &device) != ZX_OK) {
    status->Log("Error: Block device could not be set up");
    return false;
  }

  zx::time end_time = zx::deadline_after(duration);
  uint64_t num_tests = 1;

  do {
    zx::time test_start = zx::clock::get_monotonic();
    if (FlashIo(device, bytes_to_test, actual_transfer_size, /*is_write_test=*/true) != ZX_OK) {
      status->Log("Error writing to vmo.");
      return false;
    }
    zx::duration test_duration = zx::clock::get_monotonic() - test_start;
    status->Log("Test %4ld: Write: %0.3fs, throughput: %0.2f MiB/s", num_tests,
                DurationToSecs(test_duration),
                bytes_to_test / (DurationToSecs(test_duration) * 1024 * 1024));

    test_start = zx::clock::get_monotonic();
    if (FlashIo(device, bytes_to_test, actual_transfer_size, /*is_write_test=*/false) != ZX_OK) {
      status->Log("Error reading from vmo.");
      return false;
    }
    test_duration = zx::clock::get_monotonic() - test_start;
    status->Log("Test %4ld: Read: %0.3fs, throughput: %0.2f MiB/s", num_tests,
                DurationToSecs(test_duration),
                bytes_to_test / (DurationToSecs(test_duration) * 1024 * 1024));

    num_tests++;
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
