// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/writer.h"

#include <memory>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/mtl/tasks/message_loop.h"

namespace tracing {
namespace writer {
namespace {

auto NewArgumentList(TraceWriter& writer) {
  int i = 0;
  return MakeArgumentList(writer, "int32", int32_t(42), "int64", int64_t(-42),
                          "double", 42.42, "cstring", "constant", "dstring",
                          std::string("dynamic"), "pointer", &i, "koid",
                          Koid(1 << 10));
}

struct WriterTest : public ::testing::Test {
  WriterTest() {
    mx::vmo buffer;
    mx::eventpair fence;
    assert(MX_OK == mx::vmo::create(100000, 0u, &buffer));
    assert(MX_OK == mx::eventpair::create(0u, &fence, &control_));
    StartTracing(std::move(buffer), std::move(fence), {"cat"},
                 [this](TraceDisposition disposition) { loop_.QuitNow(); });
  }

  ~WriterTest() {
    StopTracing();
    loop_.Run();
  }

 private:
  mtl::MessageLoop loop_;
  mx::eventpair control_;
};

TEST_F(WriterTest, StringRegistrationAndRetrieval) {
  TraceWriter writer = TraceWriter::Prepare();

  const char* t1 = "test";
  const char* t2 = "test";
  const char* t3 = "different";
  auto ref1 = writer.RegisterString(t1);
  auto ref2 = writer.RegisterString(t2);
  auto ref3 = writer.RegisterString(t3);
  EXPECT_TRUE(ref1.encoded_value() == ref2.encoded_value());
  EXPECT_TRUE(ref1.encoded_value() != ref3.encoded_value());
  EXPECT_EQ(ref1.encoded_value(), writer.RegisterString(t1).encoded_value());
  EXPECT_EQ(ref2.encoded_value(), writer.RegisterString(t2).encoded_value());
  EXPECT_EQ(ref3.encoded_value(), writer.RegisterString(t3).encoded_value());
}

TEST_F(WriterTest, BulkStringRegistrationAndRetrieval) {
  TraceWriter writer = TraceWriter::Prepare();
  std::map<std::unique_ptr<std::string>, StringRef> ids;

  for (int i = 1; i < 4095; i++) {
    auto value = std::make_unique<std::string>(fxl::StringPrintf("%d", i));
    StringRef string_ref = writer.RegisterString(value->c_str());
    EXPECT_NE(0u, string_ref.encoded_value());
    ids.emplace(std::move(value), std::move(string_ref));
  }

  for (const auto& pair : ids) {
    EXPECT_EQ(pair.second.encoded_value(),
              writer.RegisterString(pair.first->c_str()).encoded_value());
  }
}

TEST_F(WriterTest, ThreadRegistration) {
  TraceWriter writer = TraceWriter::Prepare();

  EXPECT_NE(0u, writer.RegisterCurrentThread().encoded_value());
}

TEST_F(WriterTest, BulkThreadRegistration) {
  TraceWriter writer = TraceWriter::Prepare();
  EXPECT_NE(0u, writer.RegisterCurrentThread().encoded_value());

  std::vector<std::thread> threads;
  for (unsigned int i = 0; i < 10; i++)
    threads.emplace_back([]() {
      TraceWriter writer = TraceWriter::Prepare();
      EXPECT_NE(0u, writer.RegisterCurrentThread().encoded_value());
    });

  for (auto& thread : threads) {
    if (thread.joinable())
      thread.join();
  }
}

TEST_F(WriterTest, EventWriting) {
  TraceWriter writer = TraceWriter::Prepare();
  ThreadRef thread_ref = writer.RegisterCurrentThread();
  StringRef category_ref = writer.RegisterString("cat", true);
  StringRef name_ref = writer.RegisterString("name");

  writer.WriteDurationBeginEventRecord(GetTicksNow(), thread_ref, category_ref,
                                       name_ref);
  writer.WriteDurationEndEventRecord(GetTicksNow(), thread_ref, category_ref,
                                     name_ref);
  writer.WriteAsyncBeginEventRecord(GetTicksNow(), thread_ref, category_ref,
                                    name_ref, 42);
  writer.WriteAsyncInstantEventRecord(GetTicksNow(), thread_ref, category_ref,
                                      name_ref, 42);
  writer.WriteAsyncEndEventRecord(GetTicksNow(), thread_ref, category_ref,
                                  name_ref, 42);

  writer.WriteFlowBeginEventRecord(GetTicksNow(), thread_ref, category_ref,
                                   name_ref, 42);
  writer.WriteFlowStepEventRecord(GetTicksNow(), thread_ref, category_ref,
                                  name_ref, 42);
  writer.WriteFlowEndEventRecord(GetTicksNow(), thread_ref, category_ref,
                                 name_ref, 42);
}

TEST_F(WriterTest, EventWritingMultiThreaded) {
  std::vector<std::thread> threads;
  for (size_t i = 0; i < 10; i++)
    threads.emplace_back([]() {
      TraceWriter writer = TraceWriter::Prepare();
      ThreadRef thread_ref = writer.RegisterCurrentThread();
      StringRef category_ref = writer.RegisterString("cat", true);
      StringRef name_ref = writer.RegisterString("name");

      writer.WriteDurationBeginEventRecord(GetTicksNow(), thread_ref,
                                           category_ref, name_ref);
      writer.WriteDurationEndEventRecord(GetTicksNow(), thread_ref,
                                         category_ref, name_ref);
      writer.WriteAsyncBeginEventRecord(GetTicksNow(), thread_ref, category_ref,
                                        name_ref, 42);
      writer.WriteAsyncInstantEventRecord(GetTicksNow(), thread_ref,
                                          category_ref, name_ref, 42);
      writer.WriteAsyncEndEventRecord(GetTicksNow(), thread_ref, category_ref,
                                      name_ref, 42);
      writer.WriteFlowBeginEventRecord(GetTicksNow(), thread_ref, category_ref,
                                       name_ref, 42);
      writer.WriteFlowStepEventRecord(GetTicksNow(), thread_ref, category_ref,
                                      name_ref, 42);
      writer.WriteFlowEndEventRecord(GetTicksNow(), thread_ref, category_ref,
                                     name_ref, 42);
    });

  for (auto& thread : threads)
    if (thread.joinable())
      thread.join();
}

TEST_F(WriterTest, EventWritingWithArguments) {
  TraceWriter writer = TraceWriter::Prepare();
  ThreadRef thread_ref = writer.RegisterCurrentThread();
  StringRef category_ref = writer.RegisterString("cat", true);
  StringRef name_ref = writer.RegisterString("name");

  writer.WriteDurationBeginEventRecord(GetTicksNow(), thread_ref, category_ref,
                                       name_ref, NewArgumentList(writer));

  writer.WriteDurationEndEventRecord(GetTicksNow(), thread_ref, category_ref,
                                     name_ref, NewArgumentList(writer));

  writer.WriteAsyncBeginEventRecord(GetTicksNow(), thread_ref, category_ref,
                                    name_ref, 42, NewArgumentList(writer));

  writer.WriteAsyncInstantEventRecord(GetTicksNow(), thread_ref, category_ref,
                                      name_ref, 42, NewArgumentList(writer));

  writer.WriteAsyncEndEventRecord(GetTicksNow(), thread_ref, category_ref,
                                  name_ref, 42, NewArgumentList(writer));

  writer.WriteFlowBeginEventRecord(GetTicksNow(), thread_ref, category_ref,
                                   name_ref, 42, NewArgumentList(writer));

  writer.WriteFlowStepEventRecord(GetTicksNow(), thread_ref, category_ref,
                                  name_ref, 42, NewArgumentList(writer));

  writer.WriteFlowEndEventRecord(GetTicksNow(), thread_ref, category_ref,
                                 name_ref, 42, NewArgumentList(writer));
}

TEST_F(WriterTest, EventWritingWithArgumentsMultiThreaded) {
  std::vector<std::thread> threads;

  for (size_t i = 0; i < 10; i++) {
    threads.emplace_back([]() {
      TraceWriter writer = TraceWriter::Prepare();
      ThreadRef thread_ref = writer.RegisterCurrentThread();
      StringRef category_ref = writer.RegisterString("cat", true);
      StringRef name_ref = writer.RegisterString("name");

      writer.WriteDurationBeginEventRecord(GetTicksNow(), thread_ref,
                                           category_ref, name_ref,
                                           NewArgumentList(writer));

      writer.WriteDurationEndEventRecord(GetTicksNow(), thread_ref,
                                         category_ref, name_ref,
                                         NewArgumentList(writer));

      writer.WriteAsyncBeginEventRecord(GetTicksNow(), thread_ref, category_ref,
                                        name_ref, 42, NewArgumentList(writer));

      writer.WriteAsyncInstantEventRecord(GetTicksNow(), thread_ref,
                                          category_ref, name_ref, 42,
                                          NewArgumentList(writer));

      writer.WriteAsyncEndEventRecord(GetTicksNow(), thread_ref, category_ref,
                                      name_ref, 42, NewArgumentList(writer));

      writer.WriteFlowBeginEventRecord(GetTicksNow(), thread_ref, category_ref,
                                       name_ref, 42, NewArgumentList(writer));

      writer.WriteFlowStepEventRecord(GetTicksNow(), thread_ref, category_ref,
                                      name_ref, 42, NewArgumentList(writer));

      writer.WriteFlowEndEventRecord(GetTicksNow(), thread_ref, category_ref,
                                     name_ref, 42, NewArgumentList(writer));
    });
  }

  for (auto& thread : threads)
    if (thread.joinable())
      thread.join();
}

}  // namespace
}  // namespace writer
}  // namespace tracing
