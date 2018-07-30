// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock_pose_buffer_provider.h"

#include <lib/async-loop/cpp/loop.h>
#include "lib/component/cpp/startup_context.h"

namespace mock_pose_buffer_provider {

MockPoseBufferProviderApp::MockPoseBufferProviderApp()
    : context_(component::StartupContext::CreateFromStartupInfo()) {
  context_->outgoing().AddPublicService(bindings_.GetHandler(this));
}

void MockPoseBufferProviderApp::SetPoseBuffer(::zx::vmo buffer,
                                              uint32_t num_entries,
                                              uint64_t base_time,
                                              uint64_t time_interval) {
  buffer_ = std::move(buffer);
  num_entries_ = num_entries;
  base_time_ = base_time;
  time_interval_ = time_interval;
}

}  // namespace mock_pose_buffer_provider

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  mock_pose_buffer_provider::MockPoseBufferProviderApp app;
  loop.Run();
  return 0;
}