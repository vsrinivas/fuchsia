// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/view_associate_framework/mock_hit_tester.h"

#include "lib/ftl/logging.h"

namespace mojo {
namespace ui {

MockHitTester::MockHitTester() {}

MockHitTester::~MockHitTester() {}

void MockHitTester::SetNextResult(
    mojo::PointFPtr point,
    mojo::gfx::composition::HitTestResultPtr result) {
  FTL_DCHECK(point);
  FTL_DCHECK(result);

  point_ = point.Pass();
  result_ = result.Pass();
}

void MockHitTester::HitTest(mojo::PointFPtr point,
                            const HitTestCallback& callback) {
  FTL_DCHECK(point);

  if (point.Equals(point_)) {
    point_.reset();
    callback.Run(result_.Pass());
  } else {
    callback.Run(mojo::gfx::composition::HitTestResult::New());
  }
}

}  // namespace ui
}  // namespace mojo
