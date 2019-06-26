// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/environment/trace.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/connectivity/overnet/lib/vocabulary/optional.h"

using testing::Field;
using testing::Mock;
using testing::StrEq;
using testing::StrictMock;

namespace overnet {
namespace trace_test {

class MockRenderer : public TraceRenderer {
 public:
  MOCK_METHOD1(Render, void(TraceOutput));
  MOCK_METHOD2(NoteParentChild, void(Op, Op));
};

TEST(Trace, Simple) {
  StrictMock<MockRenderer> sink_impl;
  ScopedRenderer renderer(&sink_impl);

  auto outputs = [&](Optional<const char*> message, auto fn) {
    if (message) {
      EXPECT_CALL(sink_impl,
                  Render(Field(&TraceOutput::message, StrEq(*message))));
    }
    fn();
    Mock::VerifyAndClearExpectations(&sink_impl);
  };

#ifdef NDEBUG
  ScopedSeverity{Severity::DEBUG}, outputs(Nothing, [&] {
    OVERNET_TRACE(DEBUG) << "Hello "
                         << "World";
  });
#else
  ScopedSeverity{Severity::DEBUG}, outputs("Hello World", [&] {
    OVERNET_TRACE(DEBUG) << "Hello "
                         << "World";
  });
#endif
  ScopedSeverity{Severity::DEBUG}, outputs("Hello World", [&] {
    OVERNET_TRACE(ERROR) << "Hello "
                         << "World";
  });
  ScopedSeverity{Severity::DEBUG}, outputs("Hello World", [&] {
    OVERNET_TRACE(TRACE) << "Hello "
                         << "World";
  });
  ScopedSeverity{Severity::TRACE}, outputs("Hello World", [&] {
    OVERNET_TRACE(ERROR) << "Hello "
                         << "World";
  });
  ScopedSeverity{Severity::ERROR}, outputs("Hello World", [&] {
    OVERNET_TRACE(ERROR) << "Hello "
                         << "World";
  });
  ScopedSeverity{Severity::ERROR}, outputs(Nothing, [&] {
    OVERNET_TRACE(DEBUG) << "Hello "
                         << "World";
  });
}

}  // namespace trace_test
}  // namespace overnet
