// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <utility>
#include <vector>

#include <fs/journal/data_streamer.h>
#include <storage/operation/buffered_operation.h>

namespace fs {

void DataStreamer::StreamData(storage::UnbufferedOperation operation) {
  ZX_DEBUG_ASSERT(operation.op.type == storage::OperationType::kWrite);
  const size_t max_chunk_blocks = (3 * writeback_capacity_) / 4;
  uint64_t delta_blocks = std::min(operation.op.length, max_chunk_blocks);
  while (operation.op.length > 0) {
    // If enqueueing these blocks could push us past the writeback buffer capacity
    // when combined with all previous writes, break this transaction into a smaller
    // chunk first.
    if (operations_.BlockCount() + delta_blocks > max_chunk_blocks) {
      IssueOperations();
    }

    storage::UnbufferedOperation partial_operation = {.vmo = zx::unowned_vmo(operation.vmo->get()),
                                                      {
                                                          .type = storage::OperationType::kWrite,
                                                          .vmo_offset = operation.op.vmo_offset,
                                                          .dev_offset = operation.op.dev_offset,
                                                          .length = delta_blocks,
                                                      }};
    operations_.Add(std::move(partial_operation));
    operation.op.vmo_offset += delta_blocks;
    operation.op.dev_offset += delta_blocks;
    operation.op.length -= delta_blocks;
    delta_blocks = std::min(operation.op.length, max_chunk_blocks);
  }
}

fs::Journal::Promise DataStreamer::Flush() {
  // Issue locally buffered operations, to ensure that all data passed through |StreamData()|
  // has been issued to the executor.
  IssueOperations();

  // Return the joined result of all data operations that have been issued.
  return fit::join_promise_vector(std::move(promises_))
      .then([](fit::context& context,
               fit::result<std::vector<fit::result<void, zx_status_t>>>& result) mutable
            -> fit::result<void, zx_status_t> {
        ZX_ASSERT_MSG(result.is_ok(), "join_promise_vector should only return success type");
        // If any of the intermediate promises fail, return the first seen error status.
        for (const auto& intermediate_result : result.value()) {
          if (intermediate_result.is_error()) {
            return fit::error(intermediate_result.error());
          }
        }
        return fit::ok();
      });
}

void DataStreamer::IssueOperations() {
  auto operations = operations_.TakeOperations();
  if (!operations.size()) {
    return;
  }
  // Reserve space within the writeback buffer.
  fs::Journal::Promise work = journal_->WriteData(std::move(operations));
  // Initiate the writeback operation, tracking the completion of the write.
  promises_.push_back(fit::schedule_for_consumer(journal_, std::move(work)).promise());
}

}  // namespace fs
