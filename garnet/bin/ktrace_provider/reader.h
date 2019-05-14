// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_KTRACE_PROVIDER_READER_H_
#define GARNET_BIN_KTRACE_PROVIDER_READER_H_

#include <iterator>

#include <src/lib/files/unique_fd.h>
#include <src/lib/fxl/macros.h>
#include <lib/zircon-internal/ktrace.h>

namespace ktrace_provider {

class Reader {
 public:
  Reader(const char* buffer, size_t buffer_size);
  virtual ~Reader() = default;

  const ktrace_header_t* ReadNextRecord();

  size_t number_bytes_read() const { return number_bytes_read_; }
  size_t number_records_read() const { return number_records_read_; }

 protected:
  inline size_t AvailableBytes() const {
    return std::distance(current_, marker_);
  }

  virtual void ReadMoreData() = 0;

  const char* current_;
  const char* marker_;
  const char* end_;
  size_t number_bytes_read_ = 0;
  size_t number_records_read_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(Reader);
};

}  // namespace ktrace_provider

#endif  // GARNET_BIN_KTRACE_PROVIDER_READER_H_
