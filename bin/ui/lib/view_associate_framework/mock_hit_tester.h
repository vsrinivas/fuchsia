// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_VIEW_ASSOCIATE_FRAMEWORK_MOCK_HIT_TESTER_H_
#define APPS_MOZART_LIB_VIEW_ASSOCIATE_FRAMEWORK_MOCK_HIT_TESTER_H_

#include <unordered_map>

#include "apps/mozart/services/composition/interfaces/hit_tests.mojom.h"
#include "lib/ftl/macros.h"

namespace mozart {

class MockHitTester : public HitTester {
 public:
  MockHitTester();
  ~MockHitTester() override;

  // Sets the next hit test result.
  void SetNextResult(mojo::PointFPtr point, HitTestResultPtr result);

  // |HitTester|
  void HitTest(mojo::PointFPtr point, const HitTestCallback& callback) override;

 private:
  mojo::PointFPtr point_;
  HitTestResultPtr result_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MockHitTester);
};

}  // namespace mozart

#endif  // APPS_MOZART_LIB_VIEW_ASSOCIATE_FRAMEWORK_MOCK_HIT_TESTER_H_
