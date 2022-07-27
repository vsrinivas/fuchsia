// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/screenshot/screenshot_manager.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "src/ui/scenic/lib/screen_capture/screen_capture.h"

namespace screenshot {

ScreenshotManager::ScreenshotManager(
    bool use_flatland, std::shared_ptr<allocation::Allocator> allocator_,
    std::shared_ptr<flatland::Renderer> renderer, GetRenderables get_renderables,
    TakeGfxScreenshot take_gfx_screenshot,
    std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> buffer_collection_importers,
    fuchsia::math::SizeU display_size)
    : use_flatland_(use_flatland),
      take_gfx_screenshot_(std::move(take_gfx_screenshot)),
      renderer_(renderer),
      get_renderables_(std::move(get_renderables)),
      buffer_collection_importers_(std::move(buffer_collection_importers)),
      display_size_(display_size) {
  FX_DCHECK(renderer_);
}

void ScreenshotManager::CreateBinding(
    fidl::InterfaceRequest<fuchsia::ui::composition::Screenshot> request) {
  if (use_flatland_) {
    // Create the ScreenCapture instance that will do the heavy lifting.
    fidl::InterfacePtr<fuchsia::ui::composition::ScreenCapture> screen_capture_ptr;

    std::unique_ptr<ScreenCapture> screen_capture = std::make_unique<ScreenCapture>(
        screen_capture_ptr.NewRequest(), buffer_collection_importers_, renderer_,
        [this]() { return get_renderables_(); });

    bindings_.AddBinding(std::make_unique<screenshot::FlatlandScreenshot>(
                             std::move(screen_capture), allocator_, display_size_,
                             [this](screenshot::FlatlandScreenshot* sc) {
                               bindings_.CloseBinding(sc, ZX_ERR_SHOULD_WAIT);
                             }),
                         std::move(request));
  } else {
    bindings_.AddBinding(std::make_unique<screenshot::GfxScreenshot>(
                             [this](fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) {
                               take_gfx_screenshot_(std::move(callback));
                             },
                             [this](screenshot::GfxScreenshot* sc) {
                               bindings_.CloseBinding(sc, ZX_ERR_SHOULD_WAIT);
                             }),
                         std::move(request));
  }
}

}  // namespace screenshot
