// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include "lib/component/cpp/startup_context.h"

namespace mock_pose_buffer_provider {

// Basic mock for the PoseBufferProvider interface designed to conform to the
// interface and drive it to build while being as simple as possible otherwise.
class MockPoseBufferProviderApp : public fuchsia::ui::gfx::PoseBufferProvider {
 public:
  MockPoseBufferProviderApp();
  void SetPoseBuffer(::zx::vmo buffer, uint32_t num_entries, uint64_t base_time,
                     uint64_t time_interval) override;

 private:
  MockPoseBufferProviderApp(const MockPoseBufferProviderApp&) = delete;
  MockPoseBufferProviderApp& operator=(const MockPoseBufferProviderApp&) =
      delete;

  std::unique_ptr<component::StartupContext> context_;
  fidl::BindingSet<PoseBufferProvider> bindings_;

  ::zx::vmo buffer_;
  uint32_t num_entries_;
  uint64_t base_time_;
  uint64_t time_interval_;
};

}  // namespace mock_pose_buffer_provider