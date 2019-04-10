// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_MOCK_POSE_BUFFER_PROVIDER_H_
#define GARNET_LIB_UI_GFX_TESTS_MOCK_POSE_BUFFER_PROVIDER_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

namespace mock_pose_buffer_provider {

// Basic mock for the PoseBufferProvider interface designed to conform to the
// interface and drive it to build while being as simple as possible otherwise.
class MockPoseBufferProviderApp : public fuchsia::ui::gfx::PoseBufferProvider {
 public:
  MockPoseBufferProviderApp();
  void SetPoseBuffer(::zx::vmo buffer, uint32_t num_entries, int64_t base_time,
                     uint64_t time_interval) override;

 private:
  MockPoseBufferProviderApp(const MockPoseBufferProviderApp&) = delete;
  MockPoseBufferProviderApp& operator=(const MockPoseBufferProviderApp&) =
      delete;

  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<PoseBufferProvider> bindings_;

  ::zx::vmo buffer_;
  uint32_t num_entries_;
  int64_t base_time_;
  uint64_t time_interval_;
};

}  // namespace mock_pose_buffer_provider

#endif  // GARNET_LIB_UI_GFX_TESTS_MOCK_POSE_BUFFER_PROVIDER_H_
