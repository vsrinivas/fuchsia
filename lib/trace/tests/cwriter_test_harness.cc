// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <thread>
#include <vector>

#include "apps/tracing/lib/trace/tests/cwriter_unittest.h"
#include "apps/tracing/lib/trace/writer.h"
#include "gtest/gtest.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/mtl/tasks/message_loop.h"

namespace tracing {
namespace writer {
namespace {

struct CWriterTest : public ::testing::Test {
  CWriterTest() {
    mx::vmo buffer;
    mx::eventpair fence;
    assert(MX_OK == mx::vmo::create(100000, 0u, &buffer));
    assert(MX_OK == mx::eventpair::create(0u, &fence, &control_));
    StartTracing(std::move(buffer), std::move(fence), {"cat"},
                 [this](TraceDisposition disposition) { loop_.QuitNow(); });
  }

  ~CWriterTest() {
    StopTracing();
    loop_.Run();
  }

 private:
  mtl::MessageLoop loop_;
  mx::eventpair control_;
};

TEST_F(CWriterTest, StringRegistrationAndRetrieval) {
  EXPECT_TRUE(cwriter_test_string_registration_and_retrieval());
}

TEST_F(CWriterTest, BulkStringRegistrationAndRetrieval) {
  EXPECT_TRUE(cwriter_test_bulk_string_registration_and_retrieval());
}

TEST_F(CWriterTest, ThreadRegistration) {
  EXPECT_TRUE(cwriter_test_thread_registration());
}

TEST_F(CWriterTest, BulkThreadRegistration) {
  EXPECT_TRUE(cwriter_test_bulk_thread_registration());
}

TEST_F(CWriterTest, EventWriting) {
  EXPECT_TRUE(cwriter_test_event_writing());
}

TEST_F(CWriterTest, EventWritingMultiThreaded) {
  EXPECT_TRUE(cwriter_test_event_writing_multithreaded());
}

TEST_F(CWriterTest, EventWritingWithArguments) {
  EXPECT_TRUE(cwriter_test_event_writing_with_arguments());
}

TEST_F(CWriterTest, EventWritingWithArgumentsMultiThreaded) {
  EXPECT_TRUE(cwriter_test_event_writing_with_arguments_multithreaded());
}

}  // namespace
}  // namespace writer
}  // namespace tracing
