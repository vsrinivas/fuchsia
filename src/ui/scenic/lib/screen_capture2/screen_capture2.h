// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREEN_CAPTURE2_SCREEN_CAPTURE2_H_
#define SRC_UI_SCENIC_LIB_SCREEN_CAPTURE2_SCREEN_CAPTURE2_H_

#include <fuchsia/ui/composition/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include <deque>
#include <unordered_map>

#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/screen_capture/screen_capture_buffer_collection_importer.h"

namespace screen_capture2 {

using BufferCount = uint32_t;

class ScreenCapture : public fuchsia::ui::composition::internal::ScreenCapture {
 public:
  ScreenCapture(fidl::InterfaceRequest<fuchsia::ui::composition::internal::ScreenCapture> request,
                std::shared_ptr<screen_capture::ScreenCaptureBufferCollectionImporter>
                    screen_capture_buffer_collection_importer);

  ~ScreenCapture() override;

  void Configure(fuchsia::ui::composition::internal::ScreenCaptureConfig args,
                 ConfigureCallback callback) override;

  void GetNextFrame(GetNextFrameCallback callback) override;

 private:
  void ClearImages();

  fidl::Binding<fuchsia::ui::composition::internal::ScreenCapture> binding_;

  std::shared_ptr<screen_capture::ScreenCaptureBufferCollectionImporter>
      screen_capture_buffer_collection_importer_;

  // Holds all registered images associated with the buffer index.
  std::unordered_map<uint32_t, allocation::ImageMetadata> image_ids_;

  // Indices of available buffers.
  std::deque<uint32_t> available_buffers_;
};

}  // namespace screen_capture2

#endif  // SRC_UI_SCENIC_LIB_SCREEN_CAPTURE2_SCREEN_CAPTURE2_H_
