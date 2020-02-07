// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_TESTS_MOCKS_MOCKS_H_
#define SRC_UI_SCENIC_LIB_GFX_TESTS_MOCKS_MOCKS_H_

#include "src/ui/lib/escher/flib/release_fence_signaller.h"
#include "src/ui/scenic/lib/display/display_manager.h"
#include "src/ui/scenic/lib/gfx/engine/engine.h"
#include "src/ui/scenic/lib/gfx/engine/image_pipe_updater.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/gfx_system.h"
#include "src/ui/scenic/lib/scenic/scenic.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class ReleaseFenceSignallerForTest : public escher::ReleaseFenceSignaller {
 public:
  ReleaseFenceSignallerForTest();

  void AddCPUReleaseFence(zx::event fence) override;
};

class MockImagePipeUpdater : public ImagePipeUpdater {
 public:
  MockImagePipeUpdater() : ImagePipeUpdater() {}

  scheduling::PresentId ScheduleImagePipeUpdate(
      zx::time presentation_time, fxl::WeakPtr<ImagePipeBase> image_pipe,
      std::vector<zx::event> acquire_fences, std::vector<zx::event> release_fences,
      fuchsia::images::ImagePipe::PresentImageCallback callback) {
    ++schedule_update_call_count_;
    return ++latest_present_id_;
  }

  uint64_t schedule_update_call_count_ = 0;

 private:
  scheduling::PresentId latest_present_id_ = 0;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_TESTS_MOCKS_MOCKS_H_
