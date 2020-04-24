// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_TESTS_BASIC_INTEGRATION_TESTS_H_
#define GARNET_BIN_TRACE_TESTS_BASIC_INTEGRATION_TESTS_H_

#include "garnet/bin/trace/tests/integration_test_utils.h"

namespace tracing {
namespace test {

extern const IntegrationTest kFillBufferIntegrationTest;
extern const IntegrationTest kFillBufferAndAlertIntegrationTest;
extern const IntegrationTest kSimpleIntegrationTest;

}  // namespace test
}  // namespace tracing

#endif  // GARNET_BIN_TRACE_TESTS_BASIC_INTEGRATION_TESTS_H_
