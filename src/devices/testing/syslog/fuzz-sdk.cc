// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/socket.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <cstdlib>
#include <sstream>

#include <fuzzer/FuzzedDataProvider.h>

#include "lib/syslog/structured_backend/cpp/fuchsia_syslog.h"

// use -f to get printf output from this test.

// Parses an input stream from libFuzzer and executes arbitrary
// logging commands to fuzz the structured logging backend.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  zx::socket output;
  zx::socket input;
  zx::socket::create(0, &input, &output);
  FuzzedDataProvider provider(data, size);
  enum class OP : uint8_t {
    kStringField,
    kSignedIntField,
    kUnsignedIntField,
    kDoubleField,
    kMaxValue = kDoubleField,
  };
  fuchsia_syslog::LogBuffer buffer;
  auto severity = provider.ConsumeIntegral<FuchsiaLogSeverity>();
  // Fatal crashes...
  if (severity == FUCHSIA_LOG_FATAL) {
    severity = FUCHSIA_LOG_ERROR;
  }
  auto file = provider.ConsumeRandomLengthString();
  auto line = provider.ConsumeIntegral<unsigned int>();
  auto msg = provider.ConsumeRandomLengthString();
  auto pid = provider.ConsumeIntegral<zx_koid_t>();
  auto tid = provider.ConsumeIntegral<zx_koid_t>();
  auto condition = provider.ConsumeRandomLengthString();
  buffer.BeginRecord(severity, file.data(), line, msg.data(), condition.data(), false,
                     output.borrow(), 0, pid, tid);
  while (provider.remaining_bytes()) {
    auto op = provider.ConsumeEnum<OP>();
    auto key = provider.ConsumeRandomLengthString();
    switch (op) {
      case OP::kDoubleField:
        buffer.WriteKeyValue(key.data(), provider.ConsumeFloatingPoint<double>());
        break;
      case OP::kSignedIntField: {
        int64_t value;
        if (provider.remaining_bytes() < sizeof(value)) {
          return 0;
        }
        value = provider.ConsumeIntegral<int64_t>();
        buffer.WriteKeyValue(key.data(), value);
      } break;
      case OP::kUnsignedIntField: {
        uint64_t value;
        if (provider.remaining_bytes() < sizeof(value)) {
          return 0;
        }
        value = provider.ConsumeIntegral<uint64_t>();
        buffer.WriteKeyValue(key.data(), value);
      } break;
      case OP::kStringField: {
        auto value = provider.ConsumeRandomLengthString();
        buffer.WriteKeyValue(key.data(), value.data());
      } break;
    }
  }
  buffer.FlushRecord();
  return 0;
}
