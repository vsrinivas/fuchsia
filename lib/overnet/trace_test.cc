// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trace.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "optional.h"

using testing::Field;
using testing::Mock;
using testing::StrictMock;

namespace overnet {
namespace trace_test {

class MockTraceSink : public TraceSinkInterface {
 public:
  MOCK_METHOD0(Done, void());
  MOCK_METHOD1(Trace, void(TraceOutput));
};

TEST(Trace, Simple) {
  StrictMock<MockTraceSink> sink_impl;
  TraceSink debug_sink(Severity::DEBUG, &sink_impl);
  TraceSink error_sink(Severity::ERROR, &sink_impl);
  TraceSink null_sink;

  auto outputs = [&](Optional<const char*> message, auto fn) {
    if (message) {
      EXPECT_CALL(sink_impl, Trace(Field(&TraceOutput::message, *message)));
    }
    fn();
    Mock::VerifyAndClearExpectations(&sink_impl);
  };

  outputs("Hello World", [&] {
    OVERNET_TRACE(DEBUG, debug_sink) << "Hello "
                                     << "World";
  });
  outputs("Hello World", [&] {
    OVERNET_TRACE(ERROR, debug_sink) << "Hello "
                                     << "World";
  });
  outputs("Hello World", [&] {
    OVERNET_TRACE(ERROR, error_sink) << "Hello "
                                     << "World";
  });
  outputs(Nothing, [&] {
    OVERNET_TRACE(DEBUG, error_sink) << "Hello "
                                     << "World";
  });
  outputs(Nothing, [&] {
    OVERNET_TRACE(DEBUG, null_sink) << "Hello "
                                    << "World";
  });

  EXPECT_CALL(sink_impl, Done());
}

}  // namespace trace_test
}  // namespace overnet
