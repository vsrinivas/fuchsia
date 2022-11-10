// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/writer.h"

#include <stdarg.h>
#include <zircon/assert.h>

#include "tools/kazoo/string_util.h"

Writer::Writer() {}

void Writer::Printf(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  std::string result = StringVPrintf(format, ap);
  va_end(ap);
  Puts(result);
}

void Writer::Puts(const std::string& str) {
  out_ += str;
}

void Writer::PrintSpacerLine() {
  size_t size = out_.size();
  if (size < 2 || (out_[size - 2] == '\n' && out_[size - 1] == '\n')) {
    // The last line was empty.
    return;
  }
  Puts("\n");
}

bool WriteFileIfChanged(const std::string& filename, const std::string& data) {
  std::string old_data;
  if (ReadFileToString(filename, &old_data) && old_data == data) {
    // No need to rewrite the file.
    return true;
  }

  FILE* outf = fopen(filename.c_str(), "wb");
  if (!outf) {
    fprintf(stderr, "Couldn't open '%s' for output.\n", filename.c_str());
    return false;
  }
  size_t written = fwrite(data.data(), 1, data.size(), outf);
  if (written != data.size()) {
    fprintf(stderr, "Writing file '%s' failed, %zu written but %zu expected.\n", filename.c_str(),
            written, data.size());
    return false;
  }
  if (fclose(outf) != 0) {
    fprintf(stderr, "fclose() failed writing '%s'.\n", filename.c_str());
    return false;
  }
  return true;
}
