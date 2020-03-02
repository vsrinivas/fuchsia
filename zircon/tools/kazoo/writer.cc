// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/writer.h"

#include <zircon/assert.h>

#include "tools/kazoo/string_util.h"

Writer::Writer() {}

bool Writer::Printf(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  std::string result = StringVPrintf(format, ap);
  va_end(ap);
  return Puts(result);
}

FileWriter::FileWriter() : Writer() {}

FileWriter::~FileWriter() {
  if (outf_) {
    fclose(outf_);
  }
}

bool FileWriter::Open(const std::string& filename) {
  outf_ = fopen(filename.c_str(), "wb");
  if (!outf_) {
    fprintf(stderr, "Couldn't open '%s' for output.\n", filename.c_str());
    return false;
  }
  return true;
}

bool FileWriter::Puts(const std::string& str) {
  ZX_ASSERT(outf_);
  if (!outf_) {
    return false;
  }

  size_t written = fwrite(str.c_str(), 1, str.size(), outf_);
  if (written != str.size()) {
    fprintf(stderr, "File write failed, %zu written but %zu expected.\n", written, str.size());
    return false;
  }
  return true;
}

StringWriter::StringWriter() : Writer() {}

StringWriter::~StringWriter() {}

bool StringWriter::Puts(const std::string& str) {
  out_ += str;
  return true;
}

bool WriteFileIfChanged(const std::string& filename, const std::string& data) {
  std::string old_data;
  if (ReadFileToString(filename, &old_data) && old_data == data) {
    // No need to rewrite the file.
    return true;
  }
  FileWriter writer;
  return writer.Open(filename) && writer.Puts(data);
}
