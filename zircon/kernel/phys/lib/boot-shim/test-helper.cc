// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-shim/test-helper.h>
#include <lib/zbitl/image.h>
#include <stdio.h>

#include <zxtest/zxtest.h>

namespace boot_shim::testing {

BufferOwner BufferOwner::New(size_t size) {
  BufferOwner result;
  result.owner.reset(new std::byte[size]);
  result.buffer = {result.owner.get(), size};
  return result;
}

TestHelper::TestHelper()
    :
#if defined(__Fuchsia__) || defined(__GLIBC__)
      log_(open_memstream(&log_buffer_, &log_buffer_size_))
#else
      log_(tmpfile())
#endif
{
}

TestHelper::~TestHelper() {
  if (log_) {
    fclose(log_);
  }
  free(log_buffer_);
}

BufferOwner TestHelper::GetZbiBuffer(size_t size) {
  BufferOwner result = BufferOwner::New(size);
  zbitl::Image zbi(result.buffer);
  EXPECT_TRUE(zbi.clear().is_ok());
  return result;
}

std::vector<std::string> TestHelper::LogLines() {
  rewind(log_);
  char* buffer = nullptr;
  size_t buffer_size = 0;
  std::vector<std::string> lines;
  while (!feof(log_) && !ferror(log_)) {
    ssize_t n = getline(&buffer, &buffer_size, log_);
    if (n < 0) {
      break;
    }
    lines.emplace_back(buffer, static_cast<size_t>(n) - 1);
  }
  free(buffer);
  return lines;
}

void TestHelper::ExpectLogLines(std::initializer_list<std::string_view> expected) {
  auto log = LogLines();
  EXPECT_EQ(log.size(), expected.size());
  for (size_t i = 0; i < std::min(log.size(), expected.size()); ++i) {
    std::string_view line = log[i];
    std::string_view expected_line = std::data(expected)[i];
    if (expected_line.front() == ':') {
      line = line.substr(line.find_last_of(':'));
    }
    EXPECT_STREQ(expected_line, line);
  }
}

}  // namespace boot_shim::testing
