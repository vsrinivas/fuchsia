// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace2json/trace_parser.h"

#include <src/lib/fxl/logging.h>

namespace tracing {

FuchsiaTraceParser::FuchsiaTraceParser(std::ostream* out)
    : exporter_(*out),
      reader_([this](trace::Record record) { exporter_.ExportRecord(record); },
              [](fbl::String error) { FXL_DLOG(ERROR) << error.c_str(); }) {}

FuchsiaTraceParser::~FuchsiaTraceParser() = default;

bool FuchsiaTraceParser::Parse(std::istream* in) {
  while (!in->eof()) {
    size_t bytes_read =
        in->read(buffer_.data() + buffer_end_, buffer_.size() - buffer_end_)
            .gcount();
    if (bytes_read == 0) {
      FXL_DLOG(ERROR) << "Read returned 0 bytes";
      return false;
    }
    buffer_end_ += bytes_read;

    size_t words = buffer_end_ / sizeof(uint64_t);
    trace::Chunk chunk(reinterpret_cast<const uint64_t*>(buffer_.data()),
                       words);

    if (!reader_.ReadRecords(chunk)) {
      FXL_DLOG(ERROR) << "Error parsing trace";
      return false;
    }

    size_t offset = chunk.current_byte_offset();
    memmove(buffer_.data(), buffer_.data() + offset, buffer_end_ - offset);
    buffer_end_ -= offset;
  }
  return true;
}

}  // namespace tracing
