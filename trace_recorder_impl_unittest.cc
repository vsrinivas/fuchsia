// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/trace_recorder_impl.h"

#include "gtest/gtest.h"
#include "lib/mtl/data_pipe/strings.h"
#include "mojo/public/cpp/application/application_test_base.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/utility/run_loop.h"

using TraceRecorderImplTest = mojo::test::ApplicationTestBase;

namespace tracing {
namespace {

TEST_F(TraceRecorderImplTest, RecordingMultipleChunks) {
  TraceRecorderPtr trace_recorder_ptr;
  TraceRecorderImpl trace_recorder_impl;

  mojo::Binding<TraceRecorder> trace_recorder_binding(
      &trace_recorder_impl, mojo::GetProxy(&trace_recorder_ptr));

  mojo::DataPipe data_pipe;

  trace_recorder_impl.Start(std::move(data_pipe.producer_handle));

  trace_recorder_ptr->Record("{\"key1\": \"value1\"}");
  trace_recorder_ptr->Record("{\"key2\": \"value2\"}");
  trace_recorder_ptr->Record("{\"key3\": \"value3\"}");
  trace_recorder_ptr->Record("{\"key4\": \"value4\"}");

  mojo::RunLoop::current()->RunUntilIdle();

  trace_recorder_impl.Stop();

  std::string result;
  mtl::BlockingCopyToString(std::move(data_pipe.consumer_handle), &result);
  EXPECT_EQ(
      "[{\"key1\": \"value1\"},{\"key2\": \"value2\"},{\"key3\": "
      "\"value3\"},{\"key4\": \"value4\"}]",
      result);
}

}  // namespace
}  // namespace tracing
