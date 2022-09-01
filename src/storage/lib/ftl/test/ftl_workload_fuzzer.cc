// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdarg.h>
#include <string.h>

#include <algorithm>

#include <fuzzer/FuzzedDataProvider.h>

#include "src/devices/block/drivers/ftl/tests/ftl-shell.h"
#include "src/devices/block/drivers/ftl/tests/ndm-ram-driver.h"
#include "src/storage/lib/ftl/ftln/ftlnp.h"
#include "src/storage/lib/ftl/ftln/ndm-driver.h"

constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kPagesPerBlock = 64;
constexpr uint32_t kSpareSize = 16;
constexpr uint32_t kMaxConsecutivePageWrites = 20;

// 50 blocks means 3200 pages, which is enough to have several map pages.
constexpr ftl::VolumeOptions kDefaultOptions = {.num_blocks = 50,
                                                .max_bad_blocks = 2,
                                                .block_size = kPageSize * kPagesPerBlock,
                                                .page_size = kPageSize,
                                                .eb_size = kSpareSize,
                                                .flags = 0};

__PRINTFLIKE(3, 4) void LogToStdout(const char* file, int line, const char* format, ...) {
  va_list args;
  printf("[FTL] ");
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
  printf("\n");
}

// The default FTL/NDM logger use stderr which makes it difficult to see any issues found by the
// fuzzer. Instead we output to stdout, which can be combined with `-close_fd_mask=1` to
// suppress FTL logging while fuzzing the target.
constexpr FtlLogger kStdoutLogger{
    .trace = LogToStdout,
    .debug = LogToStdout,
    .info = LogToStdout,
    .warn = LogToStdout,
    .error = LogToStdout,
};

// Don't sprinkle in errors by default, emulate half writes on power cut, and suppress log output.
constexpr TestOptions kBoringTestOptions = {.ecc_error_interval = -1,
                                            .bad_block_interval = -1,
                                            .bad_block_burst = 0,
                                            .use_half_size = false,
                                            .save_config_data = true,
                                            .power_failure_delay = -1,
                                            .emulate_half_write_on_power_failure = true,
                                            .ftl_logger = kStdoutLogger};

// Manage and verify consistency for a block device. Needs to call Verify() to update data on mount
// if the volume was previously unmounted without a flush. If Verify() ever returns false, the rest
// of the operations should not be trusted.
class ConsistencyManager {
 public:
  ConsistencyManager(uint32_t num_pages, size_t page_length);

  // Call before a write. Fills the buffer with what should be written out, storing similar info
  // locally.
  void UpdatePages(uint32_t first_page, uint32_t num_pages, uint8_t* buffer);

  // Call after successful flush. Confirms expected data range.
  void FlushComplete();

  // Call after mount. Checks the consistency of the volume against the stored data. If the stored
  // data is in a valid range, it will update the range to reflect the current values. If data is
  // invalid this method will ASSERT.
  void Verify(ftl::Volume* volume);

 private:
  struct DataVersion {
    uint64_t generation;
    uint32_t page;
  };

  void UpdatePage(uint32_t page, uint8_t* buffer);

  uint32_t num_pages_;
  size_t page_length_;
  std::vector<uint64_t> current_;
  std::vector<uint64_t> future_;
};

ConsistencyManager::ConsistencyManager(uint32_t num_pages, size_t page_length)
    : num_pages_(num_pages), page_length_(page_length), current_(num_pages_), future_(num_pages_) {
  ZX_ASSERT_MSG(page_length >= sizeof(DataVersion), "Page size is too small for consistency data.");
}

void ConsistencyManager::UpdatePages(uint32_t first_page, uint32_t num_pages, uint8_t* buffer) {
  ZX_ASSERT_MSG(first_page + num_pages - 1 < num_pages_, "Addressed pages exceeds volume.");
  for (uint32_t i = 0; i < num_pages; ++i) {
    UpdatePage(first_page++, &buffer[page_length_ * i]);
  }
}

void ConsistencyManager::UpdatePage(uint32_t page, uint8_t* buffer) {
  DataVersion* version = reinterpret_cast<DataVersion*>(buffer);
  version->page = page;
  version->generation = ++future_[page];
}

void ConsistencyManager::FlushComplete() {
  // Intentional copy. All writes for future state are committed.
  current_ = future_;
}

void ConsistencyManager::Verify(ftl::Volume* volume) {
  auto buffer = std::make_unique<uint8_t[]>(page_length_);
  for (uint32_t i = 0; i < num_pages_; ++i) {
    // If the read failed and something is expected to be there; complain.
    if (volume->Read(i, 1, buffer.get()) != ZX_OK) {
      if (current_[i] == 0) {
        continue;
      }
      ZX_ASSERT_MSG(false, "Failed to read page %u which should have valid data.\n", i);
    }
    DataVersion* version = reinterpret_cast<DataVersion*>(buffer.get());

    ZX_ASSERT_MSG(version->page == i, "Page %u contained data from page %u\n", i, version->page);

    ZX_ASSERT_MSG(version->generation >= current_[i] && version->generation <= future_[i],
                  "Page %u contained generation %" PRIu64 " but expected range [%" PRIu64
                  ", %" PRIu64 "]\n",
                  i, version->generation, current_[i], future_[i]);

    // Passed all checks. Update the internal state to match the volume.
    current_[i] = version->generation;
    future_[i] = version->generation;
  }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);

  // Set up the test fixture.
  TestOptions test_options = kBoringTestOptions;
  if (provider.ConsumeBool()) {
    test_options.ecc_error_interval =
        provider.ConsumeIntegralInRange<int>(kDefaultOptions.max_bad_blocks, 2000);
  }
  if (provider.ConsumeBool()) {
    test_options.bad_block_interval =
        provider.ConsumeIntegralInRange<int>(kPagesPerBlock * 2, 2000);
    test_options.bad_block_burst =
        provider.ConsumeIntegralInRange<int>(0, kDefaultOptions.max_bad_blocks);
  }

  auto driver_owned = std::make_unique<NdmRamDriver>(kDefaultOptions, kBoringTestOptions);
  driver_owned->Init();
  auto* driver = driver_owned.get();

  FtlShell ftl_shell;
  ftl_shell.InitWithDriver(std::move(driver_owned));

  ftl::VolumeImpl* vol = reinterpret_cast<ftl::VolumeImpl*>(ftl_shell.volume());
  uint8_t buff[kPageSize * kMaxConsecutivePageWrites] = {};
  ConsistencyManager consistency(ftl_shell.num_pages(), kPageSize);

  zx_status_t res;
  for (uint32_t i = 0; i < ftl_shell.num_pages(); ++i) {
    consistency.UpdatePages(i, 1, buff);
    res = vol->Write(i, 1, buff);
    ZX_ASSERT_MSG(res == ZX_OK, "Failed fixture write #%d: %d\n", i, res);
  }
  res = vol->Flush();
  ZX_ASSERT_MSG(res == ZX_OK, "Failed to flush fixture: %d\n", res);
  ZX_ASSERT_MSG(vol->ReAttach() == nullptr, "Failed to remount after flush.");

  while (provider.remaining_bytes() != 0) {
    // Set up for some later failure.
    driver->SetPowerFailureDelay(provider.ConsumeIntegralInRange(0, 2000));

    // Mount may fail here due to powercut, that's ok. Just move on.
    if (const char* result = vol->ReAttach(); result == nullptr) {
      int32_t next_flush = 0;
      // Start writing places. Stop at failure. Presumably due to power cut.
      while (true) {
        uint32_t page_num = provider.ConsumeIntegralInRange<uint32_t>(0, ftl_shell.num_pages() - 1);
        uint32_t end_page = std::min(
            ftl_shell.num_pages(),
            page_num + provider.ConsumeIntegralInRange<uint32_t>(1, kMaxConsecutivePageWrites));
        uint32_t length = end_page - page_num;
        consistency.UpdatePages(page_num, length, buff);
        if (vol->Write(page_num, static_cast<int>(length), buff) != ZX_OK) {
          break;
        }
        if (next_flush-- <= 0) {
          if (vol->Flush() != ZX_OK) {
            break;
          }
          consistency.FlushComplete();
          next_flush = provider.ConsumeIntegralInRange(0, 200);
        }
      }
    }

    // Re-enable power.
    driver->SetPowerFailureDelay(-1);

    // Remount should succeed here (result will be nullptr on success).
    const char* reattach_result = vol->ReAttach();
    ZX_ASSERT_MSG(reattach_result == nullptr, "Failed reattach: %s\n", reattach_result);

    // Check that all data is as expected.
    consistency.Verify(vol);
  }
  return 0;
}
