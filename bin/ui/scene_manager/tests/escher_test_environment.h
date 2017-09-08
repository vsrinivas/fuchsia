// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/escher.h"
#include "gtest/gtest.h"

class DemoHarness;

namespace scene_manager {
namespace test {

class EscherTestEnvironment {
 public:
  void SetUp(std::string tests_name);
  void TearDown();
  escher::Escher* escher() { return escher_.get(); }

 private:
  std::unique_ptr<escher::Escher> escher_;
  std::unique_ptr<DemoHarness> escher_demo_harness_;
};

}  // namespace test
}  // namespace scene_manager
