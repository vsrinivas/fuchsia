// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <thread>
#include <vector>

#include "garnet/lib/trace/tests/cwriter_unittest.h"
#include "garnet/lib/trace/writer.h"
#include "gtest/gtest.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fsl/tasks/message_loop.h"

namespace tracing {
namespace writer {
namespace {

struct CWriterTest : public ::testing::Test {
  CWriterTest() {
    zx::vmo buffer;
    zx::eventpair fence;
    assert(ZX_OK == zx::vmo::create(100000, 0u, &buffer));
    assert(ZX_OK == zx::eventpair::create(0u, &fence, &control_));
    StartTracing(std::move(buffer), std::move(fence), {"cat"},
                 [this](TraceDisposition disposition) { loop_.QuitNow(); });
  }

  ~CWriterTest() {
    StopTracing();
    loop_.Run();
  }

 private:
  fsl::MessageLoop loop_;
  zx::eventpair control_;
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
