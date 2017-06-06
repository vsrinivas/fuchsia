// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_TESTS_MOCK_HIT_TESTER_H_
#define APPS_MOZART_LIB_TESTS_MOCK_HIT_TESTER_H_

#include <unordered_map>

#include "apps/mozart/services/composition/hit_tests.fidl.h"
#include "lib/ftl/macros.h"

namespace mozart {
namespace test {

class MockHitTester : public HitTester {
 public:
  MockHitTester();
  ~MockHitTester() override;

  // Sets the next hit test result.
  void SetNextResult(PointFPtr point, HitTestResultPtr result);

  // |HitTester|
  void HitTest(PointFPtr point, const HitTestCallback& callback) override;

 private:
  PointFPtr point_;
  HitTestResultPtr result_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MockHitTester);
};

}  // namespace test
}  // namespace mozart

#endif  // APPS_MOZART_LIB_TESTS_MOCK_HIT_TESTER_H_
