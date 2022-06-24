// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREEN_CAPTURE_SCREEN_CAPTURE_MANAGER_H_
#define SRC_UI_SCENIC_LIB_SCREEN_CAPTURE_SCREEN_CAPTURE_MANAGER_H_

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include <unordered_map>

#include "screen_capture.h"
#include "screen_capture_buffer_collection_importer.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/flatland/engine/engine.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"

namespace screen_capture {

using ClientId = uint64_t;

class ScreenCaptureManager {
 public:
  ScreenCaptureManager(std::shared_ptr<flatland::Engine> engine,
                       std::shared_ptr<flatland::Renderer> renderer,
                       std::shared_ptr<flatland::FlatlandManager> flatland_manager,
                       std::vector<std::shared_ptr<allocation::BufferCollectionImporter>>
                           buffer_collection_importers);

  void CreateClient(fidl::InterfaceRequest<fuchsia::ui::composition::ScreenCapture> screen_capture);

 private:
  // We need these for rendering the scene into the client supplied buffer.
  std::shared_ptr<flatland::Engine> engine_;
  std::shared_ptr<flatland::Renderer> renderer_;
  std::shared_ptr<flatland::FlatlandManager> flatland_manager_;
  std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> buffer_collection_importers_;

  ClientId next_client_id_ = 1;

  std::unordered_map<ClientId, std::unique_ptr<ScreenCapture>> screen_capture_clients_;
};

}  // namespace screen_capture
#endif  // SRC_UI_SCENIC_LIB_SCREEN_CAPTURE_SCREEN_CAPTURE_MANAGER_H_
