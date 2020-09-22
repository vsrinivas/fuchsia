// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <fs/journal/replay.h>

#include "fuzzer_utils.h"

namespace fs {
namespace {

extern "C" int LLVMFuzzerTestOneInput(uint8_t *data, size_t size) {
  FuzzerUtils fuzz_utils(data, size);
  JournalSuperblock info;
  if (fuzz_utils.FuzzSuperblock(&info) != ZX_OK) {
    return 0;
  }
  auto journal_start = fuzz_utils.data_provider()->ConsumeIntegral<uint64_t>();
  auto journal_length = fuzz_utils.data_provider()->ConsumeIntegral<uint64_t>();
  fuzz_utils.handler()->SetJournalStart(journal_start);
  [[maybe_unused]] auto status =
      ReplayJournal(fuzz_utils.handler(), fuzz_utils.registry(), journal_start, journal_length,
                    fuzz_utils.block_size());
  return 0;
}

}  // namespace
}  // namespace fs
