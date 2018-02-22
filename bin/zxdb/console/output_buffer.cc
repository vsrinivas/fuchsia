// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/output_buffer.h"

#include "garnet/bin/zxdb/client/err.h"

namespace zxdb {

// FileOutputBuffer ------------------------------------------------------------

FileOutputBuffer::FileOutputBuffer(FILE* file) : file_(file) {}
FileOutputBuffer::~FileOutputBuffer() = default;

void FileOutputBuffer::Append(const std::string& str) {
  fprintf(file_, "%s", str.c_str());
}

void FileOutputBuffer::FormatHelp(const std::string& str) {
  fprintf(file_, "%s\n", str.c_str());
}

void FileOutputBuffer::OutputErr(const Err& err) {
  fprintf(file_, "%s\n", err.msg().c_str());
}

// StringOutputBuffer ----------------------------------------------------------

StringOutputBuffer::StringOutputBuffer() = default;
StringOutputBuffer::~StringOutputBuffer() = default;

void StringOutputBuffer::Append(const std::string& str) {
  str_.append(str);
}

void StringOutputBuffer::FormatHelp(const std::string& str) {
  str_.append(str);
}

void StringOutputBuffer::OutputErr(const Err& err) {
  str_.append(err.msg());
}

}  // namespace zxdb
