// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREEN_CAPTURE2_SCREEN_CAPTURE2_H_
#define SRC_UI_SCENIC_LIB_SCREEN_CAPTURE2_SCREEN_CAPTURE2_H_

#include <fuchsia/ui/composition/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include <deque>
#include <unordered_map>

#include "lib/zx/eventpair.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/scenic/lib/flatland/engine/engine.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"
#include "src/ui/scenic/lib/screen_capture/screen_capture_buffer_collection_importer.h"

namespace screen_capture2 {

namespace test {
class ScreenCapture2Test;
}  // namespace test

using BufferCount = uint32_t;
using GetRenderables = std::function<flatland::Renderables()>;
using Rectangle2D = escher::Rectangle2D;

class ScreenCapture : public fuchsia::ui::composition::internal::ScreenCapture {
 public:
  ScreenCapture(std::shared_ptr<screen_capture::ScreenCaptureBufferCollectionImporter>
                    screen_capture_buffer_collection_importer,
                std::shared_ptr<flatland::Renderer> renderer, GetRenderables get_renderables);

  ~ScreenCapture() override;

  void Configure(fuchsia::ui::composition::internal::ScreenCaptureConfig args,
                 ConfigureCallback callback) override;

  void GetNextFrame(GetNextFrameCallback callback) override;

  // Called by GetNextFrame() and ScreenCapture2Manager when a new frame should be rendered. If
  // there are no available buffers or MaybeRenderFrame() is currently processing, it will return
  // and do nothing.
  void MaybeRenderFrame();

 private:
  friend class test::ScreenCapture2Test;

  void ClearImages();

  void HandleRender(uint32_t buffer_index, uint64_t timestamp);

  void HandleBufferRelease(uint32_t buffer_index);

  // For validating calls in tests only.
  bool get_client_received_last_frame() { return client_received_last_frame_; }

  std::shared_ptr<screen_capture::ScreenCaptureBufferCollectionImporter>
      screen_capture_buffer_collection_importer_;

  std::shared_ptr<flatland::Renderer> renderer_;

  // Holds all registered images associated with the buffer index.
  std::unordered_map<uint32_t, allocation::ImageMetadata> image_ids_;

  // Indices of available buffers.
  std::deque<uint32_t> available_buffers_;

  // Holds all server tokens associated with the buffer index.
  std::unordered_map<uint32_t, zx::eventpair> buffer_server_tokens_;

  // Holds the events passed into Render() during the current call of MaybeRenderFrame().
  std::vector<zx::event> current_release_fences_;

  // Used as state for calls to GetNextFrame() to ensure two calls cannot overlap.
  std::optional<ScreenCapture::GetNextFrameCallback> current_callback_;

  // The last frame produced according to the system has been rendered into a client buffer. Used to
  // correctly return a new frame immediately or wait for the next frame to be produced.
  bool client_received_last_frame_ = false;

  // Acts as a lock to MaybeRenderFrame() so it can not be used while it is still on a previous
  // call.
  // TODO(fxbug.dev/104367): If we make ScreenCapture multi-threaded, this will need to be a mutex.
  bool render_frame_in_progress_ = false;

  GetRenderables get_renderables_;

  // Should be last.
  fxl::WeakPtrFactory<ScreenCapture> weak_factory_;
};

}  // namespace screen_capture2

#endif  // SRC_UI_SCENIC_LIB_SCREEN_CAPTURE2_SCREEN_CAPTURE2_H_
