// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_BLOCK_DISPATCHER_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_BLOCK_DISPATCHER_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <zircon/errors.h>

#include <fbl/ref_counted.h>

#include "src/virtualization/bin/vmm/device/phys_mem.h"

// An abstraction around a data source for a block device.
class BlockDispatcher {
 public:
  virtual ~BlockDispatcher() = default;

  using Callback = fit::function<void(zx_status_t)>;

  virtual fpromise::promise<void, zx_status_t> Sync() = 0;

  virtual fpromise::promise<void, zx_status_t> ReadAt(void* data, uint64_t size, uint64_t off) = 0;
  virtual fpromise::promise<void, zx_status_t> WriteAt(const void* data, uint64_t size,
                                                       uint64_t off) = 0;

  struct Request {
    void* data;
    uint64_t size;
    uint64_t off;
  };
  // ReadBatch/WriteBatch have a default implementation that simply calls ReadAt/WriteAt so
  // dispatchers that don't benefit from batching requests do not have to implement these methods.
  virtual fpromise::promise<void, zx_status_t> ReadBatch(const std::vector<Request>& requests);
  virtual fpromise::promise<void, zx_status_t> WriteBatch(const std::vector<Request>& requests);
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
                              fuchsia::io::VmoFlags vmo_flags,
                              NestedBlockDispatcherCallback callback);

// Creates a BlockDispatcher based on another BlockDispatcher, but stores writes
// in memory.
void CreateVolatileWriteBlockDispatcher(uint64_t capacity, uint32_t block_size,
                                        std::unique_ptr<BlockDispatcher> base,
                                        NestedBlockDispatcherCallback callback);

// Creates a BlockDispatcher based on another BlockDispatcher that is a QCOW
// image.
void CreateQcowBlockDispatcher(std::unique_ptr<BlockDispatcher> base, fpromise::executor& executor,
                               NestedBlockDispatcherCallback callback);

// Creates a BlockDispatcher based on fuchsia.hardware.block.Block.
void CreateRemoteBlockDispatcher(zx::channel client, const PhysMem& phys_mem,
                                 NestedBlockDispatcherCallback callback);

// Joins on the vector of promises and returns a promise that returns ok if all the input promises
// complete successfully. If any promise completes with an error, that error will be provided by
// the returned promise.
//
// If multiple input promises complete with an error it is undefined which error will be surfaced
// here.
fpromise::promise<void, zx_status_t> JoinAndFlattenPromises(
    std::vector<fpromise::promise<void, zx_status_t>> promises);

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_BLOCK_DISPATCHER_H_
