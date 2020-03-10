// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE2JSON_TRACE_PARSER_H_
#define GARNET_BIN_TRACE2JSON_TRACE_PARSER_H_

#include <stdint.h>

#include <array>
#include <ostream>
#include <vector>

#include <trace-engine/fields.h>
#include <trace-reader/reader.h>

#include "garnet/lib/trace_converters/chromium_exporter.h"

namespace tracing {

class FuchsiaTraceParser {
 public:
  explicit FuchsiaTraceParser(std::ostream* out);
  ~FuchsiaTraceParser();

  bool ParseComplete(std::istream*);

 private:
  static constexpr size_t kReadBufferSize = trace::RecordFields::kMaxRecordSizeBytes * 4;
  ChromiumExporter exporter_;
  std::array<char, kReadBufferSize> buffer_;
  // The number of bytes of |buffer_| in use.
  size_t buffer_end_ = 0;
  trace::TraceReader reader_;
};

}  // namespace tracing

#endif  // GARNET_BIN_TRACE2JSON_TRACE_PARSER_H_
