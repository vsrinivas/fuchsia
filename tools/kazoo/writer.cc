// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/writer.h"

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

Writer::Writer() {}

bool Writer::Printf(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  std::string result = fxl::StringVPrintf(format, ap);
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
    FXL_LOG(ERROR) << "Couldn't open '" << filename << "' for output.";
    return false;
  }
  return true;
}

bool FileWriter::Puts(const std::string& str) {
  FXL_DCHECK(outf_);
  if (!outf_) {
    return false;
  }

  size_t written = fwrite(str.c_str(), 1, str.size(), outf_);
  if (written != str.size()) {
    FXL_LOG(ERROR) << "File write failed, " << written << " written but " << str.size()
                   << " expected.";
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
