// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREENSHOT_FLATLAND_SCREENSHOT_H_
#define SRC_UI_SCENIC_LIB_SCREENSHOT_FLATLAND_SCREENSHOT_H_

#include <fuchsia/ui/composition/cpp/fidl.h>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/scenic/lib/allocation/allocator.h"
#include "src/ui/scenic/lib/screen_capture/screen_capture.h"

namespace screenshot {

using allocation::Allocator;
using screen_capture::ScreenCapture;

class FlatlandScreenshot : public fuchsia::ui::composition::Screenshot {
 public:
  FlatlandScreenshot(std::unique_ptr<ScreenCapture> screen_capturer,
                     std::shared_ptr<Allocator> allocator, fuchsia::math::SizeU display_size,
                     fit::function<void(FlatlandScreenshot*)> destroy_instance_function);

  ~FlatlandScreenshot() override;

  // |fuchsia::ui::composition::Screenshot|
  void Take(fuchsia::ui::composition::ScreenshotTakeRequest format, TakeCallback callback) override;

 private:
  void HandleFrameRender();

  std::unique_ptr<screen_capture::ScreenCapture> screen_capturer_;
  fuchsia::sysmem::AllocatorPtr sysmem_allocator_;
  std::shared_ptr<Allocator> flatland_allocator_;

  fuchsia::math::SizeU display_size_;

  // The buffer collection where the display gets rendered into.
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info_{};

  // Called when this instance should be destroyed.
  fit::function<void(FlatlandScreenshot*)> destroy_instance_function_;

  // The client-supplied callback to be fired after the screenshot occurrs.
  TakeCallback callback_ = nullptr;
  std::shared_ptr<async::WaitOnce> render_wait_;

  // Used to ensure that the first Take() call happens after the asynchronous sysmem buffer
  // allocation.
  zx::event init_event_;
  std::shared_ptr<async::WaitOnce> init_wait_;

  // Should be last.
  fxl::WeakPtrFactory<FlatlandScreenshot> weak_factory_;
};

}  // namespace screenshot

#endif  // SRC_UI_SCENIC_LIB_SCREENSHOT_FLATLAND_SCREENSHOT_H_
