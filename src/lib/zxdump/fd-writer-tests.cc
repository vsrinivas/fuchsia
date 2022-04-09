// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <lib/zxdump/fd-writer.h>
#include <stdio.h>
#include <unistd.h>

#include <string_view>
#include <thread>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

namespace {

using namespace std::literals;

zxdump::ByteView AsBytes(std::string_view s) {
  return {reinterpret_cast<const std::byte*>(s.data()), s.size()};
}

constexpr std::string_view kTestData = "foobarbazquuxchunk\0\0\0more"sv;
const auto kChunk = AsBytes("chunk"sv);
const auto kMore = AsBytes("more"sv);

TEST(ZxdumpTests, FdWriterToFile) {
  FILE* tmpf = tmpfile();
  ASSERT_TRUE(tmpf);
  auto close_tmpf = fit::defer([tmpf]() { fclose(tmpf); });

  fbl::unique_fd tmpfd(dup(fileno(tmpf)));
  ASSERT_TRUE(tmpfd);

  zxdump::FdWriter writer(std::move(tmpfd));

  auto accum_fragment = writer.AccumulateFragmentsCallback();
  auto write_chunk = writer.WriteCallback();

  size_t offset = 0;
  constexpr size_t kExpectedOffset = 3 + 3 + 3 + 4;
  for (std::string_view s : {"foo"sv, "bar"sv, "baz"sv, "quux"sv}) {
    auto frag = AsBytes(s);
    auto result = accum_fragment(offset, frag);
    EXPECT_TRUE(result.is_ok());
    offset += frag.size();
  }
  EXPECT_EQ(offset, kExpectedOffset);

  auto frags_result = writer.WriteFragments();
  ASSERT_TRUE(frags_result.is_ok());
  EXPECT_EQ(frags_result.value(), offset);

  auto write_result = write_chunk(offset, kChunk);
  EXPECT_TRUE(write_result.is_ok());

  offset += kChunk.size() + 3;
  write_result = write_chunk(offset, kMore);
  EXPECT_TRUE(write_result.is_ok());

  // Now verify the data written to the file.
  rewind(tmpf);
  EXPECT_FALSE(ferror(tmpf));
  char buf[128];
  static_assert(sizeof(buf) > kTestData.size());
  size_t n = fread(buf, 1, sizeof(buf), tmpf);
  EXPECT_FALSE(ferror(tmpf));
  ASSERT_LE(n, sizeof(buf));
  EXPECT_EQ(n, kTestData.size());
  std::string_view tmpf_contents{buf, n};
  EXPECT_EQ(tmpf_contents, kTestData);
}

class PipeReader {
 public:
  explicit PipeReader(fbl::unique_fd pipe)
      // Note the order of initialization here matters!
      : pipe_(std::move(pipe)),
        pipe_buf_size_(fpathconf(pipe_.get(), _PC_PIPE_BUF)),
        thread_(std::thread(&PipeReader::ReaderThread, this)) {}

  // This must be called before destruction and nothing else after it.
  std::string Finish() && {
    thread_.join();
    return std::move(contents_);
  }

  ~PipeReader() { EXPECT_FALSE(thread_.joinable()); }

 private:
  static constexpr char kFillByte = 0x55;

  // The reader thread will append everything read from the pipe to the string.
  void ReaderThread() {
    ssize_t n;
    do {
      size_t contents_size = contents_.size();
      contents_.append(pipe_buf_size_, kFillByte);
      n = read(pipe_.get(), &contents_[contents_size], pipe_buf_size_);
      ASSERT_GE(n, 0) << "read: " << strerror(errno);
      contents_.resize(contents_size + n);
    } while (n > 0);
  }

  std::string contents_;
  fbl::unique_fd pipe_;
  size_t pipe_buf_size_;
  std::thread thread_;
};

TEST(ZxdumpTests, FdWriterToPipe) {
  fbl::unique_fd read_pipe, write_pipe;
  {
    int fds[2];
    ASSERT_EQ(0, pipe(fds)) << "pipe: " << strerror(errno);
    read_pipe.reset(fds[0]);
    write_pipe.reset(fds[1]);
  }

  PipeReader reader(std::move(read_pipe));
  std::optional<zxdump::FdWriter> writer(std::in_place, std::move(write_pipe));

  auto accum_fragment = writer->AccumulateFragmentsCallback();
  auto write_chunk = writer->WriteCallback();

  size_t offset = 0;
  constexpr size_t kExpectedOffset = 3 + 3 + 3 + 4;
  for (std::string_view s : {"foo"sv, "bar"sv, "baz"sv, "quux"sv}) {
    auto frag = AsBytes(s);
    auto result = accum_fragment(offset, frag);
    EXPECT_TRUE(result.is_ok());
    offset += frag.size();
  }
  EXPECT_EQ(offset, kExpectedOffset);

  auto frags_result = writer->WriteFragments();
  ASSERT_TRUE(frags_result.is_ok());
  EXPECT_EQ(frags_result.value(), offset);

  auto write_result = write_chunk(offset, kChunk);
  EXPECT_TRUE(write_result.is_ok());

  offset += kChunk.size() + 3;
  write_result = write_chunk(offset, kMore);
  EXPECT_TRUE(write_result.is_ok());

  // This closes the write side of the pipe so the reader can finish.
  writer.reset();

  std::string contents = std::move(reader).Finish();
  EXPECT_EQ(contents, kTestData);
}

}  // namespace
