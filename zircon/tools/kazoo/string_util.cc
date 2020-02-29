// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/string_util.h"

#include <errno.h>
#include <stdarg.h>
#include <zircon/assert.h>

#include <memory>

namespace {

void StringVAppendfHelper(std::string* dest, const char* format, va_list ap) {
  // Size of the small stack buffer to use first. This should be kept in sync
  // with the numbers in StringPrintfTest.StringPrintf_Boundary.
  constexpr size_t kStackBufferSize = 1024u;

  // First, try with a small buffer on the stack.
  char stack_buf[kStackBufferSize];
  // Copy |ap| (which can only be used once), in case we need to retry.
  va_list ap_copy;
  va_copy(ap_copy, ap);
  int result = vsnprintf(stack_buf, kStackBufferSize, format, ap_copy);
  va_end(ap_copy);
  if (result < 0) {
    // As far as I can tell, we'd only get |EOVERFLOW| if the result is so large
    // that it can't be represented by an |int| (in which case retrying would be
    // futile), so Chromium's implementation is wrong.
    ZX_ASSERT(false);
    return;
  }
  // |result| should be the number of characters we need, not including the
  // terminating null. However, |vsnprintf()| always null-terminates!
  size_t output_size = static_cast<size_t>(result);
  // Check if the output fit into our stack buffer. This is "<" not "<=", since
  // |vsnprintf()| will null-terminate.
  if (output_size < kStackBufferSize) {
    // It fit.
    dest->append(stack_buf, static_cast<size_t>(result));
    return;
  }

  // Since we have the required output size, we can just heap allocate that.
  // (Add 1 because |vsnprintf()| will always null-terminate.)
  size_t heap_buf_size = output_size + 1u;
  std::unique_ptr<char[]> heap_buf(new char[heap_buf_size]);
  result = vsnprintf(heap_buf.get(), heap_buf_size, format, ap);
  if (result < 0 || static_cast<size_t>(result) > output_size) {
    ZX_ASSERT(false);
    return;
  }
  ZX_ASSERT(static_cast<size_t>(result) == output_size);
  dest->append(heap_buf.get(), static_cast<size_t>(result));
}

}  // namespace

bool ReadFileToString(const std::string& path, std::string* result) {
  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) {
    fprintf(stderr, "couldn't open '%s'\n", path.c_str());
    return false;
  }
  fseek(fp, 0, SEEK_END);
  result->resize(ftell(fp));
  rewind(fp);
  fread(&(*result)[0], 1, result->size(), fp);
  fclose(fp);
  return true;
}

std::string StringPrintf(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  std::string rv;
  StringVAppendfHelper(&rv, format, ap);
  va_end(ap);
  return rv;
}

std::string StringVPrintf(const char* format, va_list ap) {
  std::string rv;
  StringVAppendfHelper(&rv, format, ap);
  return rv;
}

std::string TrimString(const std::string& str, const std::string& chars_to_trim) {
  size_t start_index = str.find_first_not_of(chars_to_trim);
  if (start_index == std::string::npos) {
    return std::string();
  }
  size_t end_index = str.find_last_not_of(chars_to_trim);
  return str.substr(start_index, end_index - start_index + 1);
}

std::vector<std::string> SplitString(const std::string& input, char delimiter,
                                     WhitespaceHandling whitespace) {
  std::vector<std::string> result;

  auto start = input.begin();
  for (auto end = start; end != input.end(); start = end + 1) {
    end = start;
    while (end != input.end() && *end != delimiter) {
      ++end;
    }
    if (whitespace == kKeepWhitespace) {
      result.push_back(std::string(start, end));
    } else {
      result.push_back(TrimString(std::string(start, end), " \t\r\n"));
    }
  }

  return result;
}

int64_t StringToInt(const std::string& str) {
  return static_cast<int64_t>(strtoll(str.c_str(), nullptr, 10));
}

uint64_t StringToUInt(const std::string& str) {
  return static_cast<uint64_t>(strtoull(str.c_str(), nullptr, 10));
}

bool StartsWith(const std::string& str, const std::string& prefix) {
  return str.compare(0, prefix.size(), prefix) == 0;
}
