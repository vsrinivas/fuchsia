// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/block_dispatcher.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/vmo.h>
#include <zircon/status.h>

#include <bitmap/rle-bitmap.h>
#include <safemath/checked_math.h>

#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/virtualization/bin/vmm/device/block.h"
#include "src/virtualization/bin/vmm/device/qcow.h"
#include "src/virtualization/bin/vmm/device/request_queue.h"

static_assert(fuchsia::io::MAX_BUF % kBlockSectorSize == 0,
              "Maximum buffer size is not a multiple of sector size");
static constexpr size_t kMaxBufSectors = fuchsia::io::MAX_BUF / kBlockSectorSize;

// Maximum number of active requests.
//
// If we exceed this, we will start queueing new requests until earlier requests complete.
constexpr size_t kMaxInFlightRequests = 64;

// Dispatcher that fulfills block requests using Fuchsia IO.
class FileBlockDispatcher : public BlockDispatcher {
 public:
  explicit FileBlockDispatcher(async_dispatcher_t* dispatcher, fuchsia::io::FilePtr file)
      : file_(std::move(file)), queue_(dispatcher, kMaxInFlightRequests) {}

 private:
  void Sync(Callback callback) override {
    TRACE_DURATION("machina", "FileBlockDispatcher::Sync");
    queue_.Dispatch([this, callback = std::move(callback)](RequestQueue::Request request) mutable {
      file_->Sync([request = std::move(request),
                   callback = std::move(callback)](fuchsia::io::Node2_Sync_Result result) mutable {
        if (result.is_err()) {
          callback(result.err());
        } else {
          callback(ZX_OK);
        }
      });
    });
  }

  void ReadAt(void* data, uint64_t size, uint64_t off, Callback callback) override {
    TRACE_DURATION("machina", "FileBlockDispatcher::ReadAt", "size", size, "off", off);
    auto io_guard = fbl::MakeRefCounted<IoGuard>(std::move(callback));
    auto addr = static_cast<uint8_t*>(data);
    for (uint64_t at = 0; at < size; at += fuchsia::io::MAX_BUF) {
      auto len = std::min<uint64_t>(size - at, fuchsia::io::MAX_BUF);
      queue_.Dispatch([this, io_guard, len, addr, at, off](RequestQueue::Request request) mutable {
        auto read_complete =
            [io_guard = std::move(io_guard), len, begin = addr + at,
             request = std::move(request)](fuchsia::io::File2_ReadAt_Result result) mutable {
              if (result.is_err()) {
                io_guard->SetStatus(result.err());
              } else {
                const std::vector<uint8_t>& buf = result.response().data;
                if (buf.size() != len) {
                  io_guard->SetStatus(ZX_ERR_IO);
                } else {
                  memcpy(begin, buf.data(), buf.size());
                }
              }
            };
        file_->ReadAt(len, off + at, std::move(read_complete));
      });
    }
  }

  void WriteAt(const void* data, uint64_t size, uint64_t off, Callback callback) override {
    TRACE_DURATION("machina", "FileBlockDispatcher::WriteAt", "size", size, "off", off);
    auto io_guard = fbl::MakeRefCounted<IoGuard>(std::move(callback));
    auto addr = static_cast<const uint8_t*>(data);
    for (uint64_t at = 0; at < size; at += fuchsia::io::MAX_BUF) {
      // Make a copy of the data.
      auto len = std::min<uint64_t>(size - at, fuchsia::io::MAX_BUF);
      auto begin = addr + at;
      std::vector<uint8_t> buf(begin, begin + len);

      // Enqueue the request.
      queue_.Dispatch([this, io_guard, len, off, at,
                       buf = std::move(buf)](RequestQueue::Request request) mutable {
        auto write_complete = [io_guard = std::move(io_guard), len, request = std::move(request)](
                                  fuchsia::io::File2_WriteAt_Result result) mutable {
          if (result.is_err()) {
            io_guard->SetStatus(result.err());
          } else if (result.response().actual_count != len) {
            io_guard->SetStatus(ZX_ERR_IO);
          }
        };
        file_->WriteAt(std::move(buf), off + at, std::move(write_complete));
      });
    }
  }

  fuchsia::io::FilePtr file_;
  RequestQueue queue_;
};

void CreateFileBlockDispatcher(async_dispatcher_t* dispatcher, fuchsia::io::FilePtr file,
                               NestedBlockDispatcherCallback callback) {
  file->GetAttr([dispatcher, file = std::move(file), callback = std::move(callback)](
                    zx_status_t status, fuchsia::io::NodeAttributes attrs) mutable {
    FX_CHECK(status == ZX_OK) << "Failed to get attributes";
    auto disp = std::make_unique<FileBlockDispatcher>(dispatcher, std::move(file));
    callback(attrs.content_size, kBlockSectorSize, std::move(disp));
  });
}

// Dispatcher that fulfills block requests using Fuchsia IO and a VMO.
class VmoBlockDispatcher : public BlockDispatcher {
 public:
  VmoBlockDispatcher(fuchsia::io::FilePtr file, zx::vmo vmo, size_t vmo_size, uintptr_t vmar_addr)
      : file_(std::move(file)), vmo_(std::move(vmo)), vmo_size_(vmo_size), vmar_addr_(vmar_addr) {}

 private:
  fuchsia::io::FilePtr file_;
  const zx::vmo vmo_;
  const size_t vmo_size_;
  const uintptr_t vmar_addr_;

  void Sync(Callback callback) override {
    TRACE_DURATION("machina", "VmoBlockDispatcher::Sync");
    file_->Sync([callback = std::move(callback)](fuchsia::io::Node2_Sync_Result result) mutable {
      if (result.is_err()) {
        callback(result.err());
      } else {
        callback(ZX_OK);
      }
    });
  }

  void ReadAt(void* data, uint64_t size, uint64_t off, Callback callback) override {
    TRACE_DURATION("machina", "VmoBlockDispatcher::ReadAt", "size", size, "off", off);
    if (size + off < size || size + off > vmo_size_) {
      callback(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    memcpy(data, reinterpret_cast<const void*>(vmar_addr_ + off), size);
    callback(ZX_OK);
  }

  void WriteAt(const void* data, uint64_t size, uint64_t off, Callback callback) override {
    TRACE_DURATION("machina", "VmoBlockDispatcher::WriteAt", "size", size, "off", off);
    if (size + off < size || size + off > vmo_size_) {
      callback(ZX_ERR_OUT_OF_RANGE);
      return;
    }
    memcpy(reinterpret_cast<void*>(vmar_addr_ + off), data, size);
    callback(ZX_OK);
  }
};

void CreateVmoBlockDispatcher(async_dispatcher_t* dispatcher, fuchsia::io::FilePtr file,
                              uint32_t vmo_flags, NestedBlockDispatcherCallback callback) {
  file->GetBuffer(
      vmo_flags, [dispatcher, file = std::move(file), vmo_flags, callback = std::move(callback)](
                     zx_status_t status, fuchsia::mem::BufferPtr buffer) mutable {
        // If the file is not backed by a vmo, or if we fail to get it, then fall back to a file
        // block dispatcher.
        if (status != ZX_OK) {
          FX_LOGS(INFO) << "Failed to get VMO, falling back to file dispatcher";
          CreateFileBlockDispatcher(dispatcher, std::move(file), std::move(callback));
          return;
        }
        uintptr_t addr;
        status = zx::vmar::root_self()->map(vmo_flags, 0, buffer->vmo, 0, buffer->size, &addr);
        FX_CHECK(status == ZX_OK) << "Failed to map VMO";
        auto disp = std::make_unique<VmoBlockDispatcher>(std::move(file), std::move(buffer->vmo),
                                                         buffer->size, addr);
        callback(buffer->size, kBlockSectorSize, std::move(disp));
      });
}

// Dispatcher that retains writes in-memory and delegates reads to another
// dispatcher.
class VolatileWriteBlockDispatcher : public BlockDispatcher {
 public:
  VolatileWriteBlockDispatcher(std::unique_ptr<BlockDispatcher> disp, zx::vmo vmo, size_t vmo_size,
                               uintptr_t vmar_addr)
      : disp_(std::move(disp)), vmo_(std::move(vmo)), vmo_size_(vmo_size), vmar_addr_(vmar_addr) {}

  ~VolatileWriteBlockDispatcher() override {
    zx_status_t status = zx::vmar::root_self()->unmap(vmar_addr_, vmo_size_);
    FX_CHECK(status == ZX_OK) << "Failed to unmap VMO";
  }

  void Sync(Callback callback) override {
    TRACE_DURATION("machina", "VolatileWriteBlockDispatcher::Sync");
    // Writes are synchronous, so sync is a no-op.
    callback(ZX_OK);
  }

  void ReadAt(void* data, uint64_t size, uint64_t off, Callback callback) override {
    TRACE_DURATION("machina", "VolatileWriteBlockDispatcher::ReadAt", "size", size, "off", off);
    if (!IsAccessValid(size, off)) {
      callback(ZX_ERR_INVALID_ARGS);
      return;
    }

    auto io_guard = fbl::MakeRefCounted<IoGuard>(std::move(callback));
    auto addr = static_cast<uint8_t*>(data);
    while (size > 0) {
      size_t sector = off / kBlockSectorSize;
      size_t num_sectors = size / kBlockSectorSize;
      size_t first_sector;
      bitmap_.Get(sector, sector + num_sectors, &first_sector);
      bool unallocated = first_sector == sector;
      if (unallocated) {
        // Not allocated, therefore calculate maximum unallocated read.
        num_sectors = std::min(kMaxBufSectors, num_sectors);
        bitmap_.Find(true, sector, sector + num_sectors, 1, &first_sector);
      }

      size_t read_size = (first_sector - sector) * kBlockSectorSize;
      FX_CHECK(read_size > 0);
      if (unallocated) {
        // Not Allocated, delegate to dispatcher.
        auto callback = [io_guard](zx_status_t status) {
          if (status != ZX_OK) {
            io_guard->SetStatus(status);
          }
        };
        disp_->ReadAt(addr, read_size, off, callback);
      } else {
        // Region is at least partially cached.
        auto mapped_addr = reinterpret_cast<const void*>(vmar_addr_ + off);
        memcpy(addr, mapped_addr, read_size);
      }

      off += read_size;
      addr += read_size;
      FX_CHECK(size >= read_size);
      size -= read_size;
    }
  }

  void WriteAt(const void* data, uint64_t size, uint64_t off, Callback callback) override {
    TRACE_DURATION("machina", "VolatileWriteBlockDispatcher::WriteAt", "size", size, "off", off);
    if (!IsAccessValid(size, off)) {
      callback(ZX_ERR_INVALID_ARGS);
      return;
    }

    size_t sector = off / kBlockSectorSize;
    size_t num_sectors = size / kBlockSectorSize;
    zx_status_t status = bitmap_.Set(sector, sector + num_sectors);
    if (status != ZX_OK) {
      callback(status);
      return;
    }

    auto mapped_addr = reinterpret_cast<void*>(vmar_addr_ + off);
    memcpy(mapped_addr, data, size);
    callback(ZX_OK);
  }

 private:
  std::unique_ptr<BlockDispatcher> disp_;
  zx::vmo vmo_;
  const size_t vmo_size_;
  const uintptr_t vmar_addr_;
  bitmap::RleBitmap bitmap_;

  bool IsAccessValid(uint64_t size, uint64_t off) {
    return size % kBlockSectorSize == 0 && off % kBlockSectorSize == 0 && off < vmo_size_ &&
           size <= vmo_size_ - off;
  }
};

void CreateVolatileWriteBlockDispatcher(uint64_t capacity, uint32_t block_size,
                                        std::unique_ptr<BlockDispatcher> base,
                                        NestedBlockDispatcherCallback callback) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(capacity, 0, &vmo);
  FX_CHECK(status == ZX_OK) << "Failed to create VMO";

  const char name[] = "volatile-block";
  status = vmo.set_property(ZX_PROP_NAME, name, sizeof(name));
  FX_CHECK(status == ZX_OK) << "Failed to set name of VMO";

  uintptr_t addr;
  status = zx::vmar::root_self()->map(
      ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE, 0, vmo, 0, capacity, &addr);
  FX_CHECK(status == ZX_OK) << "Failed to map VMO";

  auto disp = std::make_unique<VolatileWriteBlockDispatcher>(std::move(base), std::move(vmo),
                                                             capacity, addr);
  callback(capacity, block_size, std::move(disp));
}

// Dispatcher that reads from a QCOW image.
class QcowBlockDispatcher : public BlockDispatcher {
 public:
  QcowBlockDispatcher(std::unique_ptr<BlockDispatcher> disp, std::unique_ptr<QcowFile> file)
      : disp_(std::move(disp)), file_(std::move(file)) {}

 private:
  std::unique_ptr<BlockDispatcher> disp_;
  std::unique_ptr<QcowFile> file_;

  void Sync(Callback callback) override {
    // Writes are not supported, so sync is a no-op.
    TRACE_DURATION("machina", "QcowBlockDispatcher::Sync");
    callback(ZX_OK);
  }

  void ReadAt(void* data, uint64_t size, uint64_t off, Callback callback) override {
    TRACE_DURATION("machina", "QcowBlockDispatcher::ReadAt", "size", size, "off", off);
    file_->ReadAt(disp_.get(), data, size, off, std::move(callback));
  }

  void WriteAt(const void* data, uint64_t size, uint64_t off, Callback callback) override {
    TRACE_DURATION("machina", "QcowBlockDispatcher::WriteAt", "size", size, "off", off);
    callback(ZX_ERR_NOT_SUPPORTED);
  }
};

void CreateQcowBlockDispatcher(std::unique_ptr<BlockDispatcher> base,
                               NestedBlockDispatcherCallback callback) {
  auto base_ptr = base.get();
  auto file = std::make_unique<QcowFile>();
  auto file_ptr = file.get();
  auto load = [base = std::move(base), file = std::move(file),
               callback = std::move(callback)](zx_status_t status) mutable {
    uint64_t capacity = file->size();
    auto disp = std::make_unique<QcowBlockDispatcher>(std::move(base), std::move(file));
    callback(capacity, kBlockSectorSize, std::move(disp));
  };
  file_ptr->Load(base_ptr, std::move(load));
}

// Dispatcher that fulfills block requests using Block IO.
class RemoteBlockDispatcher : public BlockDispatcher {
 public:
  RemoteBlockDispatcher(std::unique_ptr<block_client::RemoteBlockDevice> device, storage::Vmoid id,
                        uint32_t block_size, const PhysMem& phys_mem)
      : device_(std::move(device)),
        id_(std::move(id)),
        block_size_(block_size),
        phys_mem_(phys_mem) {}

  ~RemoteBlockDispatcher() {
    zx_status_t status = device_->BlockDetachVmo(std::move(id_));
    FX_CHECK(status == ZX_OK) << "Failed to detach VMO from block device: "
                              << zx_status_get_string(status);
  }

 private:
  std::unique_ptr<block_client::RemoteBlockDevice> device_;
  storage::Vmoid id_;
  uint32_t block_size_;
  const PhysMem& phys_mem_;

  void Sync(Callback callback) override {
    TRACE_DURATION("machina", "RemoteBlockDispatcher::Sync");

    block_fifo_request_t request{.opcode = BLOCKIO_FLUSH};
    zx_status_t status = device_->FifoTransaction(&request, 1);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to send sync request";
    }
    callback(status);
  }

  void ReadAt(void* data, uint64_t size, uint64_t off, Callback callback) override {
    TRACE_DURATION("machina", "RemoteBlockDispatcher::ReadAt", "size", size, "off", off);
    block_fifo_request_t request{
        .opcode = BLOCKIO_READ,
        .vmoid = id_.get(),
        .length = safemath::checked_cast<uint32_t>(size / block_size_),
        .vmo_offset = phys_mem_.offset(data, size) / block_size_,
        .dev_offset = off / block_size_,
    };
    zx_status_t status = device_->FifoTransaction(&request, 1);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to send read request of " << size << " at " << off
                              << " block " << block_size_;
    }
    callback(status);
  }

  void WriteAt(const void* data, uint64_t size, uint64_t off, Callback callback) override {
    TRACE_DURATION("machina", "RemoteBlockDispatcher::WriteAt", "size", size, "off", off);
    block_fifo_request_t request{
        .opcode = BLOCKIO_WRITE,
        .vmoid = id_.get(),
        .length = safemath::checked_cast<uint32_t>(size / block_size_),
        .vmo_offset = phys_mem_.offset(data, size) / block_size_,
        .dev_offset = off / block_size_,
    };
    zx_status_t status = device_->FifoTransaction(&request, 1);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to send write request of " << size << " at " << off
                              << " block " << block_size_;
    }
    callback(status);
  }
};

void CreateRemoteBlockDispatcher(zx::channel client, const PhysMem& phys_mem,
                                 NestedBlockDispatcherCallback callback) {
  std::unique_ptr<block_client::RemoteBlockDevice> device;
  zx_status_t status = block_client::RemoteBlockDevice::Create(std::move(client), &device);
  FX_CHECK(status == ZX_OK) << "Failed to create block device";

  storage::Vmoid id;
  status = device->BlockAttachVmo(phys_mem.vmo(), &id);
  FX_CHECK(status == ZX_OK) << "Failed to attach VMO to block device";

  fuchsia_hardware_block_BlockInfo block_info;
  status = device->BlockGetInfo(&block_info);
  FX_CHECK(status == ZX_OK) << "Failed to get FIFO for block device";

  uint64_t capacity = block_info.block_count * block_info.block_size;
  auto disp = std::make_unique<RemoteBlockDispatcher>(std::move(device), std::move(id),
                                                      block_info.block_size, phys_mem);
  callback(capacity, block_info.block_size, std::move(disp));
}
