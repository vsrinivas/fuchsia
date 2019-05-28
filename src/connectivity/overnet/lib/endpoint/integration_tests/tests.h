// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/connectivity/overnet/lib/endpoint/integration_tests/environment.h"

namespace overnet {
namespace endpoint_integration_tests {

void NoOpTest(Env* env);
void NodeDescriptionPropagationTest(Env* env);

struct TestTimes {
  TimeStamp connected;
};

TestTimes OneMessageSrcToDest(Env* env, Slice body,
                              Optional<TimeDelta> allowed_time);

TestTimes RequestResponse(Env* env, Slice request_body, Slice response_body,
                          Optional<TimeDelta> allowed_time);

}  // namespace endpoint_integration_tests
}  // namespace overnet
