// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SCENE_MANAGER_TESTS_VK_SESSION_TEST_H_
#define GARNET_BIN_UI_SCENE_MANAGER_TESTS_VK_SESSION_TEST_H_

#include "garnet/bin/ui/scene_manager/tests/session_test.h"

namespace scene_manager {
namespace test {

class VkSessionTest : public SessionTest {
 public:
  // SessionTest virtual method.
  std::unique_ptr<Engine> CreateEngine() override;

 private:
  std::unique_ptr<escher::Escher> escher_;
};

}  // namespace test
}  // namespace scene_manager

#endif  // GARNET_BIN_UI_SCENE_MANAGER_TESTS_VK_SESSION_TEST_H_