// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/promise.h>
#include <lib/sync/completion.h>
#include <stddef.h>
#include <stdint.h>

#include <fs/journal/journal.h>
#include <storage/buffer/blocking_ring_buffer.h>

#include "fuzzer_utils.h"

namespace fs {
namespace {

extern "C" int LLVMFuzzerTestOneInput(uint8_t* data, size_t size) {
  FuzzerUtils fuzz_utils(data, size);
  JournalSuperblock info;
  size_t journal_len = fuzz_utils.data_provider()->ConsumeIntegralInRange<size_t>(0, 16);
  size_t writeback_len = fuzz_utils.data_provider()->ConsumeIntegralInRange<size_t>(0, 16);
  std::unique_ptr<storage::BlockingRingBuffer> journal_buffer;
  std::unique_ptr<storage::BlockingRingBuffer> writeback_buffer;
  if (fuzz_utils.FuzzSuperblock(&info) != ZX_OK ||
      fuzz_utils.CreateRingBuffer("journal-writeback-buffer", ReservedVmoid::kJournalVmoid,
                                  journal_len, &journal_buffer) != ZX_OK ||
      fuzz_utils.CreateRingBuffer("data-writeback-buffer", ReservedVmoid::kWritebackVmoid,
                                  writeback_len, &writeback_buffer) != ZX_OK) {
    return 0;
  }
  auto journal_start_block = fuzz_utils.data_provider()->ConsumeIntegral<uint64_t>();
  Journal journal(fuzz_utils.handler(), std::move(info), std::move(journal_buffer),
                  std::move(writeback_buffer), journal_start_block);
  while (fuzz_utils.data_provider()->remaining_bytes() != 0) {
    auto writeback_promise =
        journal.WriteData(fuzz_utils.FuzzOperation(ReservedVmoid::kWritebackVmoid))
            .and_then(journal.WriteData(fuzz_utils.FuzzOperation(ReservedVmoid::kWritebackVmoid)))
            .and_then(journal.WriteMetadata(fuzz_utils.FuzzOperation(ReservedVmoid::kJournalVmoid)))
            .and_then(journal.Sync());
    sync_completion_t sync_completion;
    journal.schedule_task(writeback_promise.then(
        [&sync_completion](
            fit::result<void, zx_status_t>& result) mutable -> fit::result<void, zx_status_t> {
          sync_completion_signal(&sync_completion);
          return result;
        }));
    sync_completion_wait(&sync_completion, ZX_TIME_INFINITE);
  }
  return 0;
}

}  // namespace
}  // namespace fs
