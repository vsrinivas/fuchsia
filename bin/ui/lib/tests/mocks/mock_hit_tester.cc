// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/tests/mocks/mock_hit_tester.h"

#include "lib/ftl/logging.h"

namespace mozart {
namespace test {

MockHitTester::MockHitTester() {}

MockHitTester::~MockHitTester() {}

void MockHitTester::SetNextResult(PointFPtr point, HitTestResultPtr result) {
  FTL_DCHECK(point);
  FTL_DCHECK(result);

  point_ = std::move(point);
  result_ = std::move(result);
}

void MockHitTester::HitTest(PointFPtr point, const HitTestCallback& callback) {
  FTL_DCHECK(point);

  if (point.Equals(point_)) {
    point_.reset();
    callback(std::move(result_));
  } else {
    callback(HitTestResult::New());
  }
}

}  // namespace test
}  // namespace mozart
