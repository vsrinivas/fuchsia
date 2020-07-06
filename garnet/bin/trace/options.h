// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_OPTIONS_H_
#define GARNET_BIN_TRACE_OPTIONS_H_

#include <cstddef>
#include <cstdint>
#include <string>

namespace tracing {

constexpr uint32_t kMinBufferSizeMegabytes = 1;
constexpr uint32_t kMaxBufferSizeMegabytes = 64;

// Individual providers can be tuned with these parameters.
struct ProviderSpec {
  std::string name;
  size_t buffer_size_in_mb;
};

enum class BufferingMode {
  // Tracing stops when the buffer is full.
  kOneshot,
  // A circular buffer.
  kCircular,
  // Double buffering.
  kStreaming,
};

struct BufferingModeSpec {
  const char* name;
  BufferingMode mode;
};

enum class Action {
  // Stop the session and write results.
  kStop,
};

const BufferingModeSpec* LookupBufferingMode(const std::string& name);

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_OPTIONS_H_
