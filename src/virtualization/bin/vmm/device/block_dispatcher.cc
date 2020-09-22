// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/block_dispatcher.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/vmo.h>

#include <bitmap/rle-bitmap.h>

#include "src/virtualization/bin/vmm/device/block.h"
#include "src/virtualization/bin/vmm/device/qcow.h"

static_assert(fuchsia::io::MAX_BUF % kBlockSectorSize == 0,
              "Maximum buffer size is not a multiple of sector size");
static constexpr size_t kMaxBufSectors = fuchsia::io::MAX_BUF / kBlockSectorSize;

// Dispatcher that fulfills block requests using Fuchsia IO.
class RawBlockDispatcher : public BlockDispatcher {
 public:
  explicit RawBlockDispatcher(fuchsia::io::FilePtr file) : file_(std::move(file)) {}

 private:
  fuchsia::io::FilePtr file_;

  void Sync(Callback callback) override {
    TRACE_DURATION("machina", "RawBlockDispatcher::Sync");
    file_->Sync(std::move(callback));
  }

  void ReadAt(void* data, uint64_t size, uint64_t off, Callback callback) override {
    TRACE_DURATION("machina", "RawBlockDispatcher::ReadAt", "size", size, "off", off);
    auto io_guard = fbl::MakeRefCounted<IoGuard>(std::move(callback));
    auto addr = static_cast<uint8_t*>(data);
    for (uint64_t at = 0; at < size; at += fuchsia::io::MAX_BUF) {
      auto len = std::min<uint64_t>(size - at, fuchsia::io::MAX_BUF);
      auto read = [io_guard, len, begin = addr + at](zx_status_t status, std::vector<uint8_t> buf) {
        if (status != ZX_OK) {
          io_guard->SetStatus(status);
        } else if (buf.size() != len) {
          io_guard->SetStatus(ZX_ERR_IO);
        } else {
          memcpy(begin, buf.data(), buf.size());
        }
      };
      file_->ReadAt(len, off + at, read);
    }
  }

  void WriteAt(const void* data, uint64_t size, uint64_t off, Callback callback) override {
    TRACE_DURATION("machina", "RawBlockDispatcher::WriteAt", "size", size, "off", off);
    auto io_guard = fbl::MakeRefCounted<IoGuard>(std::move(callback));
    auto addr = static_cast<const uint8_t*>(data);
    for (uint64_t at = 0; at < size; at += fuchsia::io::MAX_BUF) {
      auto len = std::min<uint64_t>(size - at, fuchsia::io::MAX_BUF);
      auto write = [io_guard, len](zx_status_t status, uint64_t actual) {
        if (status != ZX_OK) {
          io_guard->SetStatus(status);
        } else if (actual != len) {
          io_guard->SetStatus(ZX_ERR_IO);
        }
      };
      auto begin = addr + at;
      std::vector<uint8_t> buf(begin, begin + len);
      // TODO(fxbug.dev/12536): Add support for channel back-pressure.
      file_->WriteAt(std::move(buf), off + at, write);
    }
  }
};

void CreateRawBlockDispatcher(fuchsia::io::FilePtr file, NestedBlockDispatcherCallback callback) {
  file->GetAttr([file = std::move(file), callback = std::move(callback)](
                    zx_status_t status, fuchsia::io::NodeAttributes attrs) mutable {
    FX_CHECK(status == ZX_OK) << "Failed to get attributes " << status;
    auto disp = std::make_unique<RawBlockDispatcher>(std::move(file));
    callback(attrs.content_size, std::move(disp));
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
    file_->Sync(std::move(callback));
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

void CreateVmoBlockDispatcher(fuchsia::io::FilePtr file, uint32_t vmo_flags,
                              NestedBlockDispatcherCallback callback) {
  file->GetBuffer(vmo_flags, [file = std::move(file), vmo_flags, callback = std::move(callback)](
                                 zx_status_t status, fuchsia::mem::BufferPtr buffer) mutable {
    // If the file is not backed by a vmo, or if we fail to get it, then fall back to a raw block
    // dispatcher.
    if (status != ZX_OK) {
      CreateRawBlockDispatcher(std::move(file), std::move(callback));
      return;
    }
    uintptr_t addr;
    status = zx::vmar::root_self()->map(0, buffer->vmo, 0, buffer->size, vmo_flags, &addr);
    FX_CHECK(status == ZX_OK) << "Failed to map VMO " << status;
    auto disp = std::make_unique<VmoBlockDispatcher>(std::move(file), std::move(buffer->vmo),
                                                     buffer->size, addr);
    callback(buffer->size, std::move(disp));
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
    FX_DCHECK(status == ZX_OK);
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
      FX_DCHECK(size >= read_size);
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

void CreateVolatileWriteBlockDispatcher(size_t vmo_size, std::unique_ptr<BlockDispatcher> base,
                                        NestedBlockDispatcherCallback callback) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(vmo_size, 0, &vmo);
  FX_CHECK(status == ZX_OK) << "Failed to create VMO " << status;

  const char name[] = "volatile-block";
  status = vmo.set_property(ZX_PROP_NAME, name, sizeof(name));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to set name of VMO " << status;
  }

  uintptr_t addr;
  status = zx::vmar::root_self()->map(
      0, vmo, 0, vmo_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE, &addr);
  FX_CHECK(status == ZX_OK) << "Failed to map VMO " << status;

  auto disp = std::make_unique<VolatileWriteBlockDispatcher>(std::move(base), std::move(vmo),
                                                             vmo_size, addr);
  callback(vmo_size, std::move(disp));
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
    size_t size = file->size();
    auto disp = std::make_unique<QcowBlockDispatcher>(std::move(base), std::move(file));
    callback(size, std::move(disp));
  };
  file_ptr->Load(base_ptr, std::move(load));
}
