// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/logging_backend.h>
#include <lib/syslog/cpp/logging_backend_fuchsia_private.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/global.h>
#include <lib/syslog/logger.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <cstdlib>
#include <sstream>

#include <fuzzer/FuzzedDataProvider.h>

// use -f to get printf output from this test.

// Parses an input stream from libFuzzer and executes arbitrary
// logging commands to fuzz the structured logging backend.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);
  std::string str = provider.ConsumeRandomLengthString();
  std::string tag = provider.ConsumeRandomLengthString();
  std::string file = provider.ConsumeRandomLengthString();
  int line = provider.ConsumeIntegral<int>();
  FX_LOGF(DEBUG, tag.c_str(), "%s%d%d%lu", str.c_str(), provider.ConsumeIntegral<int>(),
          provider.ConsumeIntegral<int>(), provider.ConsumeIntegral<uint64_t>());
  fx_logger_logf_with_source(fx_log_get_logger(), FX_LOG_INFO, tag.c_str(), file.c_str(), line,
                             "%s%d%d%lu", str.c_str(), provider.ConsumeIntegral<int>(),
                             provider.ConsumeIntegral<int>(), provider.ConsumeIntegral<uint64_t>());
  return 0;
}
