// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/transaction/device_transaction_handler.h"

#include <lib/trace/event.h>

#include "trace.h"

namespace fs {

zx_status_t DeviceTransactionHandler::RunRequests(
    const std::vector<storage::BufferedOperation>& operations) {
  if (operations.empty()) {
    return ZX_OK;
  }

  TRACE_DURATION("storage", "DeviceTransactionHandler::RunRequests", "num", operations.size());
  std::vector<uint64_t> trace_flow_ids;

  // Update all the outgoing transactions to be in disk blocks.
  std::vector<block_fifo_request_t> block_requests(operations.size());
  {
    // This duration mainly exists to give the below TRACE_FLOW_BEGIN calls a context which ends
    // before the actual blocking call to |FifoTransaction|. Flow events originate from the end of
    // the duration that they were defined in.
    TRACE_DURATION("storage", "DeviceTransactionHandler::RunRequests::Enqueue");
    for (size_t i = 0; i < operations.size(); ++i) {
      auto& request = block_requests[i];
      request.vmoid = operations[i].vmoid;

      const auto& operation = operations[i].op;
      switch (operation.type) {
        case storage::OperationType::kRead:
          request.opcode = BLOCKIO_READ;
          break;
        case storage::OperationType::kWrite:
          request.opcode = BLOCKIO_WRITE;
          break;
        case storage::OperationType::kTrim:
          request.opcode = BLOCKIO_TRIM;
          break;
        default:
          ZX_DEBUG_ASSERT_MSG(false, "Unsupported operation");
      }
      // For the time being, restrict a transaction to operations of the same type. This probably
      // can be relaxed, as the concept of a transaction implies the operations take place logically
      // at the same time, so even if there's a mix of reads and writes, it doesn't make sense to
      // depend on the relative order of the operations, which is what could break with the merging
      // done by the request builder.
      ZX_DEBUG_ASSERT(operation.type == operations[0].op.type);

      request.vmo_offset = BlockNumberToDevice(operation.vmo_offset);
      request.dev_offset = BlockNumberToDevice(operation.dev_offset);
      uint64_t length = BlockNumberToDevice(operation.length);
      if (length > std::numeric_limits<decltype(request.length)>::max()) {
        return ZX_ERR_OUT_OF_RANGE;
      }
      request.length = static_cast<decltype(request.length)>(length);
      if (operation.trace_flow_id) {
        // Client provided an explicit flow ID, no need to begin a new flow.
        request.trace_flow_id = operation.trace_flow_id;
      } else {
        request.trace_flow_id = GenerateTraceId();
        TRACE_FLOW_BEGIN("storage", "BlockOp", request.trace_flow_id);
        trace_flow_ids.push_back(request.trace_flow_id);
      }
    }
  }

  zx_status_t status = GetDevice()->FifoTransaction(block_requests.data(), operations.size());

  TRACE_DURATION("storage", "DeviceTransactionHandler::RunRequests::Finish");
  for (const auto& id : trace_flow_ids) {
    TRACE_FLOW_END("storage", "BlockOp", id);
  }

  return status;
}

zx_status_t DeviceTransactionHandler::Flush() {
  block_fifo_request_t request = {.opcode = BLOCKIO_FLUSH};
  return GetDevice()->FifoTransaction(&request, 1);
}

}  // namespace fs
