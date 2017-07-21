// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"

#include "apps/mozart/services/scene/scene_manager.fidl.h"
#include "apps/mozart/src/scene_manager/display.h"
#include "apps/mozart/src/scene_manager/scene_manager_impl.h"
#include "apps/mozart/src/scene_manager/tests/mocks.h"
#include "lib/mtl/threading/thread.h"

#include "gtest/gtest.h"

namespace scene_manager {
namespace test {

class SceneManagerTest : public ::testing::Test {
 public:
  // ::testing::Test virtual method.
  void SetUp() override;

  // ::testing::Test virtual method.
  void TearDown() override;

  SessionPtr NewSession();

 protected:
  mozart2::SceneManagerPtr manager_;
  escher::impl::CommandBufferSequencer command_buffer_sequencer_;
  std::unique_ptr<Display> display_;
  std::unique_ptr<fidl::Binding<mozart2::SceneManager>> manager_binding_;
  std::unique_ptr<SceneManagerImplForTest> manager_impl_;
  std::unique_ptr<mtl::Thread> thread_;
};

}  // namespace test
}  // namespace scene_manager
