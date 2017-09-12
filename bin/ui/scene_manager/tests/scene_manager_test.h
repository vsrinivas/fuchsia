// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"

#include "lib/ui/scenic/fidl/scene_manager.fidl.h"
#include "garnet/bin/ui/scene_manager/displays/display_manager.h"
#include "garnet/bin/ui/scene_manager/scene_manager_impl.h"
#include "garnet/bin/ui/scene_manager/tests/mocks.h"
#include "lib/fsl/threading/thread.h"

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
  SceneManagerImpl* manager_impl() {
    FXL_DCHECK(manager_impl_);
    return manager_impl_.get();
  }
  Engine* engine() { return manager_impl()->engine(); }
  scenic::SceneManagerPtr manager_;
  escher::impl::CommandBufferSequencer command_buffer_sequencer_;
  DisplayManager display_manager_;
  std::unique_ptr<Display> display_;
  std::unique_ptr<fidl::Binding<scenic::SceneManager>> manager_binding_;
  std::unique_ptr<fsl::Thread> thread_;

 private:
  std::unique_ptr<SceneManagerImpl> manager_impl_;
};

}  // namespace test
}  // namespace scene_manager
