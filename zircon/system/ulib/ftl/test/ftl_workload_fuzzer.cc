// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ftl/ndm-driver.h>
#include <stdarg.h>
#include <string.h>

#include <algorithm>

#include <fuzzer/FuzzedDataProvider.h>

#include "src/devices/block/drivers/ftl/tests/ftl-shell.h"
#include "src/devices/block/drivers/ftl/tests/ndm-ram-driver.h"
#include "zircon/system/ulib/ftl/ftln/ftlnp.h"

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

  // Fill the device up with zeroes.
  ftl::VolumeImpl* vol = reinterpret_cast<ftl::VolumeImpl*>(ftl_shell.volume());
  uint8_t buff[kPageSize * kMaxConsecutivePageWrites] = {};

  zx_status_t res;
  for (uint32_t i = 0; i < ftl_shell.num_pages() - kPagesPerBlock; ++i) {
    res = vol->Write(i, 1, buff);
    ZX_ASSERT_MSG(res == ZX_OK, "Failed fixture write #%d: %d\n", i, res);
  }
  res = vol->Flush();
  ZX_ASSERT_MSG(res == ZX_OK, "Failed to flush fixture: %d\n", res);

  // Write out 01010101 patterns during the run so that we know what pages were modified.
  std::fill(std::begin(buff), std::end(buff), 0x55);

  while (provider.remaining_bytes() != 0) {
    // Set up for some later failure.
    driver->SetPowerFailureDelay(provider.ConsumeIntegralInRange(0, 2000));

    // Mount may fail here due to powercut, that's ok. Just move on.
    if (const char* result = vol->ReAttach(); result == nullptr) {
      // Start writing places. Stop at failure. Presumably due to power cut.
      int32_t next_flush = 0;
      uint32_t page_num = provider.ConsumeIntegralInRange<uint32_t>(0, ftl_shell.num_pages() - 1);
      uint32_t end_page = std::min(
          ftl_shell.num_pages(),
          page_num + provider.ConsumeIntegralInRange<uint32_t>(1, kMaxConsecutivePageWrites));
      while (vol->Write(page_num, static_cast<int>(end_page - page_num), buff) == ZX_OK) {
        if (next_flush-- <= 0) {
          if (vol->Flush() != ZX_OK) {
            break;
          }
          next_flush = provider.ConsumeIntegralInRange(0, 200);
        }
      }
    }

    // Re-enable power.
    driver->SetPowerFailureDelay(-1);

    // Remount should succeed here (result will be nullptr on success).
    const char* reattach_result = vol->ReAttach();
    ZX_ASSERT_MSG(reattach_result == nullptr, "Failed reattach: %s\n", reattach_result);

    // Check for corruption.
    std::string diagnose_result = vol->DiagnoseKnownIssues();
    ZX_ASSERT_MSG(diagnose_result.empty(), "Found known issue: %s\n", diagnose_result.c_str());
  }
  return 0;
}
