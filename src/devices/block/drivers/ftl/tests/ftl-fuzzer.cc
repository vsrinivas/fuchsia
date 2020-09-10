// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "ftl-shell.h"

namespace {

constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kNumBlocks = 300;
constexpr uint32_t kMaxBadBlocks = 300 / 20;
constexpr uint32_t kPagesPerblock = 64;
constexpr uint32_t kEbSize = 16;

class ClosedStdout {
 public:
  ClosedStdout() {
    // Save and close stdout.
    orig_stdout_ = dup(fileno(stdout));
    fclose(stdout);
  }
  ~ClosedStdout() {
    // Restore stdout. Need to flush stdout to clear its buffer.
    // TODO(fxbug.dev/39447): Remove this line once the bug is fixed.
    fflush(stdout);
    dup2(orig_stdout_, fileno(stdout));
    close(orig_stdout_);
  }

 private:
  int orig_stdout_ = 0;
};

// 300 blocks of 64 pages.
constexpr ftl::VolumeOptions kDefaultOptions = {
    kNumBlocks, kMaxBadBlocks, kPagesPerblock* kPageSize, kPageSize, kEbSize, 0};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
  ClosedStdout closed_stdout;
  FuzzedDataProvider fuzzed_data(Data, Size);

  FtlShell ftl;
  ZX_ASSERT(ftl.Init(kDefaultOptions));
  uint32_t num_pages = ftl.num_pages();
  uint32_t page_size = ftl.page_size();

  int write_pages = fuzzed_data.ConsumeIntegralInRange<int>(1, num_pages);
  size_t buffer_size = page_size * write_pages;
  std::vector<uint8_t> buffer_data = fuzzed_data.ConsumeBytes<uint8_t>(buffer_size);
  if (buffer_data.size() < buffer_size) {
    // We don't have enough data.
    return 0;
  }

  uint32_t start_page = fuzzed_data.ConsumeIntegral<uint32_t>();

  if (ftl.volume()->Write(start_page, write_pages, buffer_data.data()) != ZX_OK) {
    return 0;
  }

  ZX_ASSERT(ZX_OK == ftl.volume()->Flush());
  ZX_ASSERT(ftl.ReAttach());

  std::vector<uint8_t> read_buffer(page_size * num_pages);
  memset(read_buffer.data(), 0, read_buffer.size());
  ZX_ASSERT(ZX_OK == ftl.volume()->Read(start_page, num_pages, read_buffer.data()));

  for (uint32_t i = 0; i < buffer_data.size(); i++) {
    ZX_ASSERT(read_buffer[i] == buffer_data[i]);
  }
  return 0;
}

}  // namespace
