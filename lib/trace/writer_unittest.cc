// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/writer.h"

#include <memory>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "lib/ftl/strings/string_printf.h"

namespace tracing {
namespace writer {
namespace {

template <size_t size_of_memory_in_bytes = 100 * 1024>
struct TracingControllingFixture : public ::testing::Test {
  TracingControllingFixture() {
    mx::vmo vmo;
    assert(NO_ERROR == mx::vmo::create(size_of_memory_in_bytes, 0u, &vmo));
    StartTracing(std::move(vmo), mx::vmo(), {""});
  }

  ~TracingControllingFixture() { StopTracing(); }
};

using NamePoolTest = TracingControllingFixture<>;

TEST_F(NamePoolTest, RegistrationAndRetrieval) {
  const char* t1 = "test";
  const char* t2 = "test";
  const char* t3 = "different";
  auto ref1 = RegisterString(t1);
  auto ref2 = RegisterString(t2);
  auto ref3 = RegisterString(t3);
  EXPECT_TRUE(ref1.encoded_value() == ref2.encoded_value());
  EXPECT_TRUE(ref1.encoded_value() != ref3.encoded_value());
  EXPECT_EQ(ref1.encoded_value(), RegisterString(t1).encoded_value());
  EXPECT_EQ(ref2.encoded_value(), RegisterString(t2).encoded_value());
  EXPECT_EQ(ref3.encoded_value(), RegisterString(t3).encoded_value());
}

using BulkNamePoolTest = TracingControllingFixture<>;

TEST_F(BulkNamePoolTest, BulkRegistrationAndRetrieval) {
  std::map<std::unique_ptr<std::string>, StringRef> ids;

  for (int i = 1; i < 4095; i++) {
    auto value = std::make_unique<std::string>(ftl::StringPrintf("%d", i));
    StringRef string_ref = RegisterString(value->c_str());
    EXPECT_NE(0u, string_ref.encoded_value());
    ids.emplace(std::move(value), std::move(string_ref));
  }

  for (const auto& pair : ids) {
    EXPECT_EQ(pair.second.encoded_value(),
              RegisterString(pair.first->c_str()).encoded_value());
  }
}

using RegisterThreadTest = TracingControllingFixture<>;

TEST_F(RegisterThreadTest, Registration) {
  EXPECT_NE(0u, RegisterCurrentThread().encoded_value());
}

TEST_F(RegisterThreadTest, RegistrationForMultipleThreads) {
  EXPECT_NE(0u, RegisterCurrentThread().encoded_value());

  std::vector<std::thread> threads;
  for (unsigned int i = 0; i < 10; i++)
    threads.emplace_back(
        []() { EXPECT_NE(0u, RegisterCurrentThread().encoded_value()); });

  for (auto& thread : threads) {
    if (thread.joinable())
      thread.join();
  }
}

using WriterTest = TracingControllingFixture<>;

TEST_F(WriterTest, EventRecording) {
  WriteDurationBeginEventRecord("cat", "name");
  WriteDurationEndEventRecord("cat", "name");
  WriteAsyncBeginEventRecord("cat", "name", 42);
  WriteAsyncInstantEventRecord("cat", "name", 42);
  WriteAsyncEndEventRecord("cat", "name", 42);
}

TEST_F(WriterTest, EventRecordingMultiThreaded) {
  std::vector<std::thread> threads;
  for (size_t i = 0; i < 10; i++)
    threads.emplace_back([]() {
      WriteDurationBeginEventRecord("cat", "name");
      WriteDurationEndEventRecord("cat", "name");
      WriteAsyncBeginEventRecord("cat", "name", 42);
      WriteAsyncInstantEventRecord("cat", "name", 42);
      WriteAsyncEndEventRecord("cat", "name", 42);
    });

  for (auto& thread : threads)
    if (thread.joinable())
      thread.join();
}

TEST_F(WriterTest, EventRecordingWithArguments) {
  int i = 0;

  WriteDurationBeginEventRecord(
      "cat", "name", MakeArgument("int32", int32_t(42)),
      MakeArgument("int64", int64_t(-42)), MakeArgument("double", 42.42),
      MakeArgument("cstring", "constant"),
      MakeArgument("dstring", std::string("dynamic")),
      MakeArgument("pointer", &i), MakeArgument("koid", Koid(1 << 10)));

  WriteDurationEndEventRecord(
      "cat", "name", MakeArgument("int32", int32_t(42)),
      MakeArgument("int64", int64_t(-42)), MakeArgument("double", 42.42),
      MakeArgument("cstring", "constant"),
      MakeArgument("dstring", std::string("dynamic")),
      MakeArgument("pointer", &i), MakeArgument("koid", Koid(1 << 10)));

  WriteAsyncBeginEventRecord(
      "cat", "name", 42, MakeArgument("int32", int32_t(42)),
      MakeArgument("int64", int64_t(-42)), MakeArgument("double", 42.42),
      MakeArgument("cstring", "constant"),
      MakeArgument("dstring", std::string("dynamic")),
      MakeArgument("pointer", &i), MakeArgument("koid", Koid(1 << 10)));

  WriteAsyncInstantEventRecord(
      "cat", "name", 42, MakeArgument("int32", int32_t(42)),
      MakeArgument("int64", int64_t(-42)), MakeArgument("double", 42.42),
      MakeArgument("cstring", "constant"),
      MakeArgument("dstring", std::string("dynamic")),
      MakeArgument("pointer", &i), MakeArgument("koid", Koid(1 << 10)));

  WriteAsyncEndEventRecord(
      "cat", "name", 42, MakeArgument("int32", int32_t(42)),
      MakeArgument("int64", int64_t(-42)), MakeArgument("double", 42.42),
      MakeArgument("cstring", "constant"),
      MakeArgument("dstring", std::string("dynamic")),
      MakeArgument("pointer", &i), MakeArgument("koid", Koid(1 << 10)));
}

TEST_F(WriterTest, EventRecordingWithArgumentsMultiThreaded) {
  std::vector<std::thread> threads;

  for (size_t i = 0; i < 10; i++) {
    threads.emplace_back([]() {
      int i = 0;

      WriteDurationBeginEventRecord(
          "cat", "name", MakeArgument("int32", int32_t(42)),
          MakeArgument("int64", int64_t(-42)), MakeArgument("double", 42.42),
          MakeArgument("cstring", "constant"),
          MakeArgument("dstring", std::string("dynamic")),
          MakeArgument("pointer", &i), MakeArgument("koid", Koid(1 << 10)));

      WriteDurationEndEventRecord(
          "cat", "name", MakeArgument("int32", int32_t(42)),
          MakeArgument("int64", int64_t(-42)), MakeArgument("double", 42.42),
          MakeArgument("cstring", "constant"),
          MakeArgument("dstring", std::string("dynamic")),
          MakeArgument("pointer", &i), MakeArgument("koid", Koid(1 << 10)));

      WriteAsyncBeginEventRecord(
          "cat", "name", 42, MakeArgument("int32", int32_t(42)),
          MakeArgument("int64", int64_t(-42)), MakeArgument("double", 42.42),
          MakeArgument("cstring", "constant"),
          MakeArgument("dstring", std::string("dynamic")),
          MakeArgument("pointer", &i), MakeArgument("koid", Koid(1 << 10)));

      WriteAsyncInstantEventRecord(
          "cat", "name", 42, MakeArgument("int32", int32_t(42)),
          MakeArgument("int64", int64_t(-42)), MakeArgument("double", 42.42),
          MakeArgument("cstring", "constant"),
          MakeArgument("dstring", std::string("dynamic")),
          MakeArgument("pointer", &i), MakeArgument("koid", Koid(1 << 10)));

      WriteAsyncEndEventRecord(
          "cat", "name", 42, MakeArgument("int32", int32_t(42)),
          MakeArgument("int64", int64_t(-42)), MakeArgument("double", 42.42),
          MakeArgument("cstring", "constant"),
          MakeArgument("dstring", std::string("dynamic")),
          MakeArgument("pointer", &i), MakeArgument("koid", Koid(1 << 10)));
    });
  }

  for (auto& thread : threads)
    if (thread.joinable())
      thread.join();
}

}  // namespace
}  // namespace writer
}  // namespace tracing
