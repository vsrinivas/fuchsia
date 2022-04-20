// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/logging_backend.h>
#include <lib/syslog/cpp/logging_backend_fuchsia_private.h>
#include <lib/syslog/cpp/macros.h>
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
  enum class OP : uint8_t {
    kStringField,
    kSignedIntField,
    kUnsignedIntField,
    kDoubleField,
    kMaxValue = kDoubleField,
    kBooleanField,
  };
  syslog_backend::LogBuffer buffer;
  auto severity = provider.ConsumeIntegral<syslog::LogSeverity>();
  // Fatal crashes...
  if (severity == syslog::LOG_FATAL) {
    severity = syslog::LOG_ERROR;
  }
  auto file = provider.ConsumeRandomLengthString();
  auto line = provider.ConsumeIntegral<unsigned int>();
  auto msg = provider.ConsumeRandomLengthString();
  auto condition = provider.ConsumeRandomLengthString();
  syslog_backend::BeginRecord(&buffer, severity, file.data(), line, msg.data(), condition.data());
  while (provider.remaining_bytes()) {
    auto op = provider.ConsumeEnum<OP>();
    auto key = provider.ConsumeRandomLengthString();
    switch (op) {
      case OP::kDoubleField:
        syslog_backend::WriteKeyValue(&buffer, key.data(), provider.ConsumeFloatingPoint<double>());
        break;
      case OP::kSignedIntField: {
        int64_t value;
        if (provider.remaining_bytes() < sizeof(value)) {
          return 0;
        }
        value = provider.ConsumeIntegral<int64_t>();
        syslog_backend::WriteKeyValue(&buffer, key.data(), value);
      } break;
      case OP::kUnsignedIntField: {
        uint64_t value;
        if (provider.remaining_bytes() < sizeof(value)) {
          return 0;
        }
        value = provider.ConsumeIntegral<uint64_t>();
        syslog_backend::WriteKeyValue(&buffer, key.data(), value);
      } break;
      case OP::kStringField: {
        auto value = provider.ConsumeRandomLengthString();
        syslog_backend::WriteKeyValue(&buffer, key.data(), value.data());
      } break;
      case OP::kBooleanField: {
        syslog_backend::WriteKeyValue(&buffer, key.data(), provider.ConsumeBool());
      } break;
    }
  }
  syslog_backend::EndRecord(&buffer);
  syslog_backend::FlushRecord(&buffer);
  return 0;
}
