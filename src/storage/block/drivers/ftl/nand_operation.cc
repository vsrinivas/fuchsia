// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nand_operation.h"

#include <zircon/process.h>

#include <ddk/debug.h>
#include <ddk/driver.h>

namespace ftl {

zx_status_t NandOperation::SetDataVmo(size_t num_bytes) {
  nand_operation_t* operation = GetOperation();
  if (!operation) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status = GetVmo(num_bytes);
  if (status != ZX_OK) {
    return status;
  }

  operation->rw.data_vmo = mapper_.vmo().get();
  return ZX_OK;
}

zx_status_t NandOperation::SetOobVmo(size_t num_bytes) {
  nand_operation_t* operation = GetOperation();
  if (!operation) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status = GetVmo(num_bytes);
  if (status != ZX_OK) {
    return status;
  }

  operation->rw.oob_vmo = mapper_.vmo().get();
  return ZX_OK;
}

nand_operation_t* NandOperation::GetOperation() {
  if (!raw_buffer_) {
    CreateOperation();
  }
  return reinterpret_cast<nand_operation_t*>(raw_buffer_.get());
}

zx_status_t NandOperation::Execute(OobDoubler* parent) {
  parent->Queue(GetOperation(), OnCompletion, this);
  zx_status_t status = sync_completion_wait(&event_, ZX_SEC(60));
  sync_completion_reset(&event_);
  if (status != ZX_OK) {
    return status;
  }
  return status_;
}

// Static.
void NandOperation::OnCompletion(void* cookie, zx_status_t status, nand_operation_t* op) {
  NandOperation* operation = reinterpret_cast<NandOperation*>(cookie);
  operation->status_ = status;
  sync_completion_signal(&operation->event_);
}

zx_status_t NandOperation::GetVmo(size_t num_bytes) {
  if (mapper_.start()) {
    return ZX_OK;
  }

  return mapper_.CreateAndMap(num_bytes, "");
}

void NandOperation::CreateOperation() {
  ZX_DEBUG_ASSERT(op_size_ >= sizeof(nand_operation_t));
  raw_buffer_.reset(new char[op_size_]);

  memset(raw_buffer_.get(), 0, op_size_);
}

}  // namespace ftl.
