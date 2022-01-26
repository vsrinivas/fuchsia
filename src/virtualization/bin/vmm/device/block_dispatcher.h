// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_BLOCK_DISPATCHER_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_BLOCK_DISPATCHER_H_

#include <fuchsia/io/cpp/fidl.h>

#include <fbl/ref_counted.h>

#include "src/virtualization/bin/vmm/device/phys_mem.h"

// An abstraction around a data source for a block device.
struct BlockDispatcher {
  virtual ~BlockDispatcher() = default;

  using Callback = fit::function<void(zx_status_t)>;
  virtual void Sync(Callback callback) = 0;
  virtual void ReadAt(void* data, uint64_t size, uint64_t off, Callback callback) = 0;
  virtual void WriteAt(const void* data, uint64_t size, uint64_t off, Callback callback) = 0;
};

// Guards an IO operation.
//
// This class ensures that the given IO callback will be invoked on destruction
// and can be used to wait for mutliple IO operations to complete.
class IoGuard : public fbl::RefCounted<IoGuard> {
 public:
  explicit IoGuard(BlockDispatcher::Callback callback) : callback_(std::move(callback)) {}
  ~IoGuard() { callback_(status_); }

  void SetStatus(zx_status_t status) { status_ = status; }

 private:
  BlockDispatcher::Callback callback_;
  zx_status_t status_ = ZX_OK;
};

// Allows one BlockDispatcher to be nested within another.
//
// For example, if you have a read-only BlockDispatcher, but you want to
// enable writes by storing them in-memory, you could do the following:
//
// auto nested = [callback = std::move(callback)](
//     uint64_t capacity, uint32_t block_size,
//     std::unique_ptr<BlockDispatcher> disp) mutable {
//   CreateVolatileWriteBlockDispatcher(size, std::move(disp),
//                                      std::move(callback));
// };
// CreateFileBlockDispatcher(disp, std::move(file), std::move(nested));
using NestedBlockDispatcherCallback =
    fit::function<void(uint64_t capacity, uint32_t block_size, std::unique_ptr<BlockDispatcher>)>;

// Creates a BlockDispatcher based on a file.
void CreateFileBlockDispatcher(async_dispatcher_t* dispatcher, fuchsia::io::FilePtr file,
                               NestedBlockDispatcherCallback callback);

// Creates a BlockDispatcher based on a file, by acquiring a vmo representing the file. Falls back
// to CreateFileBlockDispatcher when failing to acquire a vmo.
void CreateVmoBlockDispatcher(async_dispatcher_t* dispatcher, fuchsia::io::FilePtr file,
                              uint32_t vmo_flags, NestedBlockDispatcherCallback callback);

// Creates a BlockDispatcher based on another BlockDispatcher, but stores writes
// in memory.
void CreateVolatileWriteBlockDispatcher(uint64_t capacity, uint32_t block_size,
                                        std::unique_ptr<BlockDispatcher> base,
                                        NestedBlockDispatcherCallback callback);

// Creates a BlockDispatcher based on another BlockDispatcher that is a QCOW
// image.
void CreateQcowBlockDispatcher(std::unique_ptr<BlockDispatcher> base,
                               NestedBlockDispatcherCallback callback);

// Creates a BlockDispatcher based on fuchsia.hardware.block.Block.
void CreateRemoteBlockDispatcher(zx::channel client, const PhysMem& phys_mem,
                                 NestedBlockDispatcherCallback callback);

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_BLOCK_DISPATCHER_H_
