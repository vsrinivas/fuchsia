// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fdio.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <cstdio>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include <fbl/unique_fd.h>

#include "garnet/bin/run_test_component/output_collector.h"
#include "gtest/gtest.h"

#define EXPECT_EQ_OUTPUT(expect, actual)                                        \
  EXPECT_EQ(std::accumulate((expect).begin(), (expect).end(), std::string("")), \
            std::accumulate((actual).begin(), (actual).end(), std::string("")))

fbl::unique_fd GetFd(std::unique_ptr<run::OutputCollector>& collector) {
  auto sock = collector->TakeServer();

  fdio_t* fdio;
  zx_status_t status = fdio_create(sock.release(), &fdio);
  ZX_ASSERT_MSG(status == ZX_OK, "Cannot create fdio from socket: %s\n",
                zx_status_get_string(status));
  fbl::unique_fd fd;
  fd.reset(fdio_bind_to_fd(fdio, -1, 3));
  ZX_ASSERT_MSG(fd.is_valid(), "Failed to create fd out of a socket.");
  return fd;
}

TEST(OutputCollector, CanWriteSimpleLines) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto collector = run::OutputCollector::Create();
  auto fd = GetFd(collector);
  std::vector<std::string> out;
  collector->CollectOutput([&](std::string s) { out.push_back(std::move(s)); }, loop.dispatcher());
  dprintf(fd.get(), "line one\n");
  fsync(fd.get());
  dprintf(fd.get(), "line two\n");
  fsync(fd.get());
  dprintf(fd.get(), "line three\n");
  fsync(fd.get());
  loop.RunUntilIdle();
  std::vector<std::string> expect = {"line one\n", "line two\n", "line three\n"};
  EXPECT_EQ_OUTPUT(expect, out);
}

TEST(OutputCollector, BrokenLines) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto collector = run::OutputCollector::Create();
  auto fd = GetFd(collector);
  std::vector<std::string> out;
  collector->CollectOutput([&](std::string s) { out.push_back(std::move(s)); }, loop.dispatcher());
  dprintf(fd.get(), "This is");
  fsync(fd.get());
  loop.RunUntilIdle();
  EXPECT_EQ(out.size(), 0u) << std::accumulate(out.begin(), out.end(), std::string(", "));

  dprintf(fd.get(), " incomplete line\n");
  fsync(fd.get());
  loop.RunUntilIdle();

  std::vector<std::string> expect = {"This is incomplete line\n"};
  EXPECT_EQ(expect, out);
}

TEST(OutputCollector, BrokenLinesWithNewLineAtBegining) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto collector = run::OutputCollector::Create();
  auto fd = GetFd(collector);
  std::vector<std::string> out;
  collector->CollectOutput([&](std::string s) { out.push_back(std::move(s)); }, loop.dispatcher());
  dprintf(fd.get(), "This is incomplete line");
  fsync(fd.get());
  loop.RunUntilIdle();
  EXPECT_EQ(out.size(), 0u) << std::accumulate(out.begin(), out.end(), std::string(", "));

  dprintf(fd.get(), "\nSecond line");
  fsync(fd.get());
  loop.RunUntilIdle();

  std::vector<std::string> expect = {"This is incomplete line\n"};
  EXPECT_EQ(expect, out);
  fd.reset();
  loop.RunUntilIdle();
  expect.push_back("Second line");
  EXPECT_EQ(expect, out);
}

TEST(OutputCollector, NoNewLine) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto collector = run::OutputCollector::Create();
  auto fd = GetFd(collector);
  std::vector<std::string> out;
  collector->CollectOutput([&](std::string s) { out.push_back(std::move(s)); }, loop.dispatcher());
  dprintf(fd.get(), "line one\n");
  fsync(fd.get());
  dprintf(fd.get(), "line without new line");
  fsync(fd.get());
  fd.reset();
  loop.RunUntilIdle();

  std::vector<std::string> expect = {"line one\n", "line without new line"};
  EXPECT_EQ_OUTPUT(expect, out);
}

TEST(OutputCollector, MultipleNewLinesInOneLine) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto collector = run::OutputCollector::Create();
  auto fd = GetFd(collector);
  std::vector<std::string> out;
  collector->CollectOutput([&](std::string s) { out.push_back(std::move(s)); }, loop.dispatcher());
  dprintf(fd.get(), "line one\nline two\nline three\n");
  fsync(fd.get());
  dprintf(fd.get(), "line four\n");
  fsync(fd.get());

  loop.RunUntilIdle();

  std::vector<std::string> expect = {"line one\nline two\nline three\n", "line four\n"};
  EXPECT_EQ_OUTPUT(expect, out);
}

TEST(OutputCollector, NewLineInMiddle) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto collector = run::OutputCollector::Create();
  auto fd = GetFd(collector);
  std::vector<std::string> out;
  collector->CollectOutput([&](std::string s) { out.push_back(std::move(s)); }, loop.dispatcher());
  dprintf(fd.get(), "line one\nline two\nline without new line");
  fsync(fd.get());
  loop.RunUntilIdle();

  std::vector<std::string> expect = {"line one\nline two\n"};
  EXPECT_EQ_OUTPUT(expect, out);

  fd.reset();
  loop.RunUntilIdle();
  expect.push_back("line without new line");
  EXPECT_EQ_OUTPUT(expect, out);
}

TEST(OutputCollector, ComplexCase) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto collector = run::OutputCollector::Create();
  auto fd = GetFd(collector);
  std::vector<std::string> out;
  collector->CollectOutput([&](std::string s) { out.push_back(std::move(s)); }, loop.dispatcher());
  dprintf(fd.get(), "line one\nline two\nline without new line");
  fsync(fd.get());
  dprintf(fd.get(), "line three\nline four\nline five");
  fsync(fd.get());
  loop.RunUntilIdle();

  std::vector<std::string> expect = {"line one\nline two\n",
                                     "line without new lineline three\nline four\n"};
  EXPECT_EQ_OUTPUT(expect, out);

  fd.reset();
  loop.RunUntilIdle();
  expect.push_back("line five");
  EXPECT_EQ_OUTPUT(expect, out);
}

TEST(OutputCollector, BigLine) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto collector = run::OutputCollector::Create();
  auto fd = GetFd(collector);
  std::vector<std::string> out;
  collector->CollectOutput([&](std::string s) { out.push_back(std::move(s)); }, loop.dispatcher());
  std::string s(3000, 'a');
  dprintf(fd.get(), "%s", s.c_str());
  fsync(fd.get());
  loop.RunUntilIdle();

  EXPECT_EQ(out.size(), 0u);  // should not get anything as no newline was sent
  dprintf(fd.get(), "\n");
  loop.RunUntilIdle();

  s.append("\n");

  std::vector<std::string> expect = {std::move(s)};
  EXPECT_EQ(expect, out);
}

namespace run {
extern size_t OC_BUFFER_THRESHOLD;
extern size_t OC_DATA_BUFFER_SIZE;

}  // namespace run

TEST(OutputCollector, TillBufferThreshold) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto collector = run::OutputCollector::Create();
  auto fd = GetFd(collector);
  std::vector<std::string> out;
  collector->CollectOutput([&](std::string s) { out.push_back(std::move(s)); }, loop.dispatcher());
  std::string s(run::OC_BUFFER_THRESHOLD, 'a');
  dprintf(fd.get(), "%s", s.c_str());
  fsync(fd.get());
  loop.RunUntilIdle();

  EXPECT_EQ(out.size(), 0u);  // should not get anything as no newline was sent
  dprintf(fd.get(), "\n");
  loop.RunUntilIdle();

  s.append("\n");

  std::vector<std::string> expect = {std::move(s)};
  EXPECT_EQ(expect, out);
}

TEST(OutputCollector, WriteBufferThresholdPlusOne) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto collector = run::OutputCollector::Create();
  auto fd = GetFd(collector);
  std::vector<std::string> out;
  collector->CollectOutput([&](std::string s) { out.push_back(std::move(s)); }, loop.dispatcher());
  std::string s(run::OC_BUFFER_THRESHOLD + 1, 'a');
  dprintf(fd.get(), "%s", s.c_str());
  fsync(fd.get());
  loop.RunUntilIdle();

  std::vector<std::string> expect = {std::move(s)};
  EXPECT_EQ(expect, out);
}

TEST(OutputCollector, NewLineAtBufferThresholdPlusOne) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto collector = run::OutputCollector::Create();
  auto fd = GetFd(collector);
  std::vector<std::string> out;
  collector->CollectOutput([&](std::string s) { out.push_back(std::move(s)); }, loop.dispatcher());
  std::string s(run::OC_BUFFER_THRESHOLD, 'a');
  s.append("\n");
  dprintf(fd.get(), "%s", s.c_str());
  fsync(fd.get());
  loop.RunUntilIdle();

  std::vector<std::string> expect = {std::move(s)};
  EXPECT_EQ(expect, out);
}

TEST(OutputCollector, NewLineAtBufferThreshold) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto collector = run::OutputCollector::Create();
  auto fd = GetFd(collector);
  std::vector<std::string> out;
  collector->CollectOutput([&](std::string s) { out.push_back(std::move(s)); }, loop.dispatcher());
  std::string s(run::OC_BUFFER_THRESHOLD - 1, 'a');
  s.append("\n");
  dprintf(fd.get(), "%s", s.c_str());
  fsync(fd.get());
  loop.RunUntilIdle();

  std::vector<std::string> expect = {std::move(s)};
  EXPECT_EQ(expect, out);
}

TEST(OutputCollector, NewLineAtBufferThresholdMinusOne) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto collector = run::OutputCollector::Create();
  auto fd = GetFd(collector);
  std::vector<std::string> out;
  collector->CollectOutput([&](std::string s) { out.push_back(std::move(s)); }, loop.dispatcher());
  std::string s(run::OC_BUFFER_THRESHOLD - 2, 'a');
  s.append("\n");
  dprintf(fd.get(), "%s", s.c_str());
  fsync(fd.get());
  loop.RunUntilIdle();

  std::vector<std::string> expect = {std::move(s)};
  EXPECT_EQ(expect, out);
}

TEST(OutputCollector, NewLineAtDataBufferSize) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto collector = run::OutputCollector::Create();
  auto fd = GetFd(collector);
  std::vector<std::string> out;
  collector->CollectOutput([&](std::string s) { out.push_back(std::move(s)); }, loop.dispatcher());
  std::string s(run::OC_DATA_BUFFER_SIZE - 1, 'a');
  s.append("\n");
  dprintf(fd.get(), "%s", s.c_str());
  fsync(fd.get());
  loop.RunUntilIdle();

  std::vector<std::string> expect = {std::move(s)};
  EXPECT_EQ(expect, out);
}

TEST(OutputCollector, NewLineAtDataBufferSizeMinusOne) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto collector = run::OutputCollector::Create();
  auto fd = GetFd(collector);
  std::vector<std::string> out;
  collector->CollectOutput([&](std::string s) { out.push_back(std::move(s)); }, loop.dispatcher());
  std::string s(run::OC_DATA_BUFFER_SIZE - 2, 'a');
  s.append("\n");
  dprintf(fd.get(), "%s", s.c_str());
  fsync(fd.get());
  loop.RunUntilIdle();

  std::vector<std::string> expect = {std::move(s)};
  EXPECT_EQ(expect, out);
}

TEST(OutputCollector, NewLineAtDataBufferSizePlusOne) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto collector = run::OutputCollector::Create();
  auto fd = GetFd(collector);
  std::vector<std::string> out;
  collector->CollectOutput([&](std::string s) { out.push_back(std::move(s)); }, loop.dispatcher());
  std::string s(run::OC_DATA_BUFFER_SIZE, 'a');
  s.append("\n");
  dprintf(fd.get(), "%s", s.c_str());
  fsync(fd.get());
  loop.RunUntilIdle();

  std::vector<std::string> expect = {std::move(s)};
  EXPECT_EQ(expect, out);
}
