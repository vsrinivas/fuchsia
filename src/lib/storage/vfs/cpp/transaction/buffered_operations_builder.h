// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_TRANSACTION_BUFFERED_OPERATIONS_BUILDER_H_
#define FS_TRANSACTION_BUFFERED_OPERATIONS_BUILDER_H_

#include <vector>

#include <zircon/assert.h>
#include <fbl/macros.h>
#include <storage/buffer/block_buffer.h>
#include <storage/operation/operation.h>
#ifdef __Fuchsia__
#include <storage/buffer/owned_vmoid.h>
#endif

namespace fs {
namespace internal {

// TODO(fxbug.dev/47947): This interface needs tidying up. For now, add a class here which stops the
// proliferation of BorrowedBuffer classes, which don't fully support the BlockBuffer interface.
class BorrowedBuffer : public storage::BlockBuffer {
 public:
#ifdef __Fuchsia__
  explicit BorrowedBuffer(vmoid_t vmoid) : vmoid_(vmoid) {}

  vmoid_t vmoid() const final { return vmoid_; }
  void* Data(size_t index) final { return nullptr; }
  const void* Data(size_t index) const final { return nullptr; }
#else
  explicit BorrowedBuffer(void* data) : data_(data) {}

  vmoid_t vmoid() const final { return 0; }
  void* Data(size_t index) final {
    ZX_ASSERT(index == 0);
    return data_;
  }
  const void* Data(size_t index) const final {
    ZX_ASSERT(index == 0);
    return data_;
  }
#endif

  size_t capacity() const final { return 0; }
  zx_handle_t Vmo() const final { return ZX_HANDLE_INVALID; }
  uint32_t BlockSize() const final { return 0; }

 private:
#ifdef __Fuchsia__
  vmoid_t vmoid_;
#else
  void* data_;
#endif
};

}  // namespace internal

// A builder which helps clients collect and coalesce BufferedOperations which target the same
// in-memory / on-disk structures.
class BufferedOperationsBuilder {
 public:
  BufferedOperationsBuilder() = default;

  // Not copyable or movable.
  BufferedOperationsBuilder(const BufferedOperationsBuilder&) = delete;
  BufferedOperationsBuilder& operator=(const BufferedOperationsBuilder&) = delete;

  // Adds a request to the list of operations.
  // Note that there is some coalescing of requests performed here, and mixing different types
  // of operations is not supported at this time.
  BufferedOperationsBuilder& Add(
      const storage::Operation& operation, storage::BlockBuffer* buffer);

  // Removes the vector of requests, and returns them to the caller.
  std::vector<storage::BufferedOperation> TakeOperations();

#ifdef __Fuchsia__
  // Adds a vmoid that needs to be detached once the operations have completed.
  void AddVmoid(storage::OwnedVmoid vmoid) {
    vmoids_.push_back(std::move(vmoid));
  }
#endif

 private:
  std::vector<storage::BufferedOperation> operations_;

#ifdef __Fuchsia__
  std::vector<storage::OwnedVmoid> vmoids_;
#endif
};

}  // namespace fs

#endif  // FS_TRANSACTION_BUFFERED_OPERATIONS_BUILDER_H_
