// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/output_buffer.h"

#include "garnet/bin/zxdb/client/err.h"

namespace zxdb {

OutputBuffer::OutputBuffer() = default;
OutputBuffer::~OutputBuffer() = default;

void OutputBuffer::Append(const std::string& str) {
  str_.append(str);
}

void OutputBuffer::FormatHelp(const std::string& str) {
  str_.append(str);
}

void OutputBuffer::OutputErr(const Err& err) {
  str_.append(err.msg());
}

void OutputBuffer::WriteToStdout() {
  fwrite(str_.data(), 1, str_.size(), stdout);
  fwrite("\n", 1, 1, stdout);
}

}  // namespace zxdb
