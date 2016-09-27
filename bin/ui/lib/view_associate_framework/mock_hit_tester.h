// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_UI_ASSOCIATES_MOCK_HIT_TESTER_H_
#define MOJO_UI_ASSOCIATES_MOCK_HIT_TESTER_H_

#include <unordered_map>

#include "apps/mozart/services/composition/interfaces/hit_tests.mojom.h"
#include "lib/ftl/macros.h"

namespace mojo {
namespace ui {

// TODO(jeffbrown): Move this someplace closer to mojo::gfx::composition
class MockHitTester : public mojo::gfx::composition::HitTester {
 public:
  MockHitTester();
  ~MockHitTester() override;

  // Sets the next hit test result.
  void SetNextResult(mojo::PointFPtr point,
                     mojo::gfx::composition::HitTestResultPtr result);

  // |HitTester|
  void HitTest(mojo::PointFPtr point, const HitTestCallback& callback) override;

 private:
  mojo::PointFPtr point_;
  mojo::gfx::composition::HitTestResultPtr result_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MockHitTester);
};

}  // namespace ui
}  // namespace mojo

#endif  // MOJO_UI_ASSOCIATES_MOCK_HIT_TESTER_H_
