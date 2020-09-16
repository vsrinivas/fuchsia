// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_OPERATION_UNBUFFERED_OPERATIONS_BUILDER_H_
#define STORAGE_OPERATION_UNBUFFERED_OPERATIONS_BUILDER_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/device/block.h>

#include <utility>
#include <vector>

#include <fbl/macros.h>
#include <storage/operation/unbuffered_operation.h>

namespace storage {

// A builder which helps clients collect and coalesce UnbufferedOperations which target the same
// in-memory / on-disk structures.
//
// This class is thread-compatible.
class UnbufferedOperationsBuilder {
 public:
  UnbufferedOperationsBuilder() : block_count_(0) {}
  UnbufferedOperationsBuilder(const UnbufferedOperationsBuilder&) = delete;
  UnbufferedOperationsBuilder& operator=(const UnbufferedOperationsBuilder&) = delete;
  UnbufferedOperationsBuilder(UnbufferedOperationsBuilder&&) = default;
  UnbufferedOperationsBuilder& operator=(UnbufferedOperationsBuilder&&) = default;
  ~UnbufferedOperationsBuilder();

  // Returns the total number of blocks in all requests.
  uint64_t BlockCount() const { return block_count_; }

  // Adds a UnbufferedOperation to the list of requests.
  //
  // Empty requests are dropped.
  void Add(const UnbufferedOperation& operation);

  // Removes the vector of requests, and returns them to the caller.
  // This resets the |UnbufferedOperationsBuilder| object.
  std::vector<UnbufferedOperation> TakeOperations();

 private:
  std::vector<UnbufferedOperation> operations_;
  uint64_t block_count_;
};

}  // namespace storage

#endif  // STORAGE_OPERATION_UNBUFFERED_OPERATIONS_BUILDER_H_
