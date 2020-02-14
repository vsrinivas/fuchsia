// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_FTL_NAND_OPERATION_H_
#define SRC_STORAGE_BLOCK_DRIVERS_FTL_NAND_OPERATION_H_

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/sync/completion.h>

#include <memory>

#include <ddk/protocol/nand.h>
#include <fbl/macros.h>

#include "oob_doubler.h"

namespace ftl {

// Wrapper for nand Queue() protocol operations.
class NandOperation {
 public:
  explicit NandOperation(size_t op_size) : op_size_(op_size) {}
  ~NandOperation() {}

  // Creates a vmo and sets the handle on the nand_operation_t.
  zx_status_t SetDataVmo(size_t num_bytes);
  zx_status_t SetOobVmo(size_t num_bytes);

  nand_operation_t* GetOperation();

  // Executes the operation and returns the final operation status.
  zx_status_t Execute(OobDoubler* parent);

  // Accessors for the memory represented by the operation's vmo.
  size_t buffer_size() const { return mapper_.size(); }
  char* buffer() const { return reinterpret_cast<char*>(mapper_.start()); }

  DISALLOW_COPY_ASSIGN_AND_MOVE(NandOperation);

 private:
  static void OnCompletion(void* cookie, zx_status_t status, nand_operation_t* op);
  zx_status_t GetVmo(size_t num_bytes);
  void CreateOperation();

  sync_completion_t event_;
  fzl::OwnedVmoMapper mapper_;
  size_t op_size_;
  zx_status_t status_ = ZX_ERR_INTERNAL;
  std::unique_ptr<char[]> raw_buffer_;
};

}  // namespace ftl.

#endif  // SRC_STORAGE_BLOCK_DRIVERS_FTL_NAND_OPERATION_H_
