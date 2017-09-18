// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/trace/writer.h"

#include "gtest/gtest.h"

namespace tracing {
namespace writer {
namespace {

TEST(WriterStubTest, AllFunctionsDoNothing) {
  EXPECT_FALSE(StartTracing(zx::vmo(), zx::eventpair(), {},
                            [](TraceDisposition disposition) {}));
  StopTracing();
  EXPECT_FALSE(IsTracingEnabled());
  EXPECT_FALSE(IsTracingEnabledForCategory("cat"));
  EXPECT_TRUE(GetEnabledCategories().empty());
  EXPECT_EQ(TraceState::kFinished, GetTraceState());
  TraceHandlerKey handler_key = AddTraceHandler([](TraceState state) {});
  RemoveTraceHandler(handler_key);

  auto writer = TraceWriter::Prepare();
  EXPECT_FALSE(writer);
  if (writer) {
    // These methods will not be reached but their symbols must be present.
    writer.RegisterString("", false);
    writer.RegisterStringCopy(std::string());
    writer.RegisterCurrentThread();
    writer.RegisterThread(0, 0);
    writer.WriteProcessDescription(0, std::string());
    writer.WriteThreadDescription(0, 0, std::string());
    writer.WriteKernelObjectRecord(0);
    writer.WriteContextSwitchRecord(0, 0, ThreadState::kDead,
                                    ThreadRef::MakeUnknown(),
                                    ThreadRef::MakeUnknown());
    writer.WriteLogRecord(0, ThreadRef::MakeUnknown(), nullptr, 0);
    writer.WriteInstantEventRecord(0, ThreadRef::MakeUnknown(),
                                   StringRef::MakeEmpty(),
                                   StringRef::MakeEmpty(), EventScope::kThread);
    writer.WriteCounterEventRecord(0, ThreadRef::MakeUnknown(),
                                   StringRef::MakeEmpty(),
                                   StringRef::MakeEmpty(), 0);
    writer.WriteDurationBeginEventRecord(0, ThreadRef::MakeUnknown(),
                                         StringRef::MakeEmpty(),
                                         StringRef::MakeEmpty());
    writer.WriteDurationEndEventRecord(0, ThreadRef::MakeUnknown(),
                                       StringRef::MakeEmpty(),
                                       StringRef::MakeEmpty());
    writer.WriteAsyncBeginEventRecord(0, ThreadRef::MakeUnknown(),
                                      StringRef::MakeEmpty(),
                                      StringRef::MakeEmpty(), 0);
    writer.WriteAsyncInstantEventRecord(0, ThreadRef::MakeUnknown(),
                                        StringRef::MakeEmpty(),
                                        StringRef::MakeEmpty(), 0);
    writer.WriteAsyncEndEventRecord(0, ThreadRef::MakeUnknown(),
                                    StringRef::MakeEmpty(),
                                    StringRef::MakeEmpty(), 0);
  }
}

}  // namespace
}  // namespace writer
}  // namespace tracing
