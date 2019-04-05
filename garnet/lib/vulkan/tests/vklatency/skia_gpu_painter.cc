// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/vulkan/tests/vklatency/skia_gpu_painter.h"

#include "src/lib/fxl/logging.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/gpu/GrBackendSurface.h"

namespace examples {

SkiaGpuPainter::SkiaGpuPainter(Swapchain* swapchain, uint32_t width,
                               uint32_t height)
    : vk_swapchain_(swapchain), width_(width), height_(height) {
  image_draw_resources_.resize(vk_swapchain_->GetNumberOfSwapchainImages());
}

SkiaGpuPainter::~SkiaGpuPainter() {}

void SkiaGpuPainter::OnInputEvent(fuchsia::ui::input::InputEvent event) {
  if (!event.is_pointer())
    return;
  const fuchsia::ui::input::PointerEvent& pointer = event.pointer();
  uint32_t pointer_id = pointer.device_id * 32 + pointer.pointer_id;
  switch (pointer.phase) {
    case fuchsia::ui::input::PointerEventPhase::DOWN:
    case fuchsia::ui::input::PointerEventPhase::MOVE:
      if (pointer.type == fuchsia::ui::input::PointerEventType::TOUCH ||
          pointer.type == fuchsia::ui::input::PointerEventType::STYLUS ||
          (pointer.type == fuchsia::ui::input::PointerEventType::MOUSE &&
           pointer.buttons & fuchsia::ui::input::kMousePrimaryButton)) {
        for (auto& image : image_draw_resources_) {
          const auto& point = SkPoint::Make(pointer.x, pointer.y);
          if (!image.paths_in_progress.count(pointer_id)) {
            image.paths_in_progress[pointer_id] = SkPath().moveTo(point);
          } else {
            image.paths_in_progress[pointer_id].lineTo(point);
          }
        }
      }
      pending_draw_ = true;
      break;
    case fuchsia::ui::input::PointerEventPhase::UP:
      for (auto& image : image_draw_resources_) {
        image.complete_paths.push_back(image.paths_in_progress[pointer_id]);
        image.paths_in_progress.erase(pointer_id);
      }
      pending_draw_ = true;
      break;
    default:
      break;
  }
}

void SkiaGpuPainter::DrawImage() {
  Swapchain::SwapchainImageResources* image =
      vk_swapchain_->GetCurrentImageResources();
  const uint32_t image_index = image->index;

  PrepareSkSurface(image);
  auto& image_draw_resource = image_draw_resources_[image_index];
  SkCanvas* canvas = image_draw_resource.sk_surface->getCanvas();

  SkPaint paint;
  paint.setColor(SK_ColorBLACK);
  paint.setAntiAlias(true);
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(.4f);
  for (auto& complete_path : image_draw_resource.complete_paths)
    canvas->drawPath(complete_path, paint);
  image_draw_resource.complete_paths.clear();
  for (auto& paths_in_progress : image_draw_resource.paths_in_progress) {
    canvas->drawPath(paths_in_progress.second, paint);
    SkPoint last_point;
    paths_in_progress.second.getLastPt(&last_point);
    paths_in_progress.second = SkPath().moveTo(last_point);
  }
  canvas->flush();

  SetImageLayout(image);
  vk_swapchain_->SwapImages();
  pending_draw_ = false;
}

bool SkiaGpuPainter::HasPendingDraw() { return pending_draw_; }

void SkiaGpuPainter::PrepareSkSurface(
    Swapchain::SwapchainImageResources* image) {
  auto& sk_surface = image_draw_resources_[image->index].sk_surface;
  if (!sk_surface) {
    SkSurfaceProps surface_props =
        SkSurfaceProps(0, SkSurfaceProps::kLegacyFontHost_InitType);
    GrVkImageInfo vk_image_info;
    vk_image_info.fImage = image->image;
    vk_image_info.fAlloc = {VK_NULL_HANDLE, 0, 0, 0};
    vk_image_info.fImageLayout = static_cast<VkImageLayout>(image->layout);
    vk_image_info.fImageTiling = VK_IMAGE_TILING_OPTIMAL;
    vk_image_info.fFormat = VK_FORMAT_B8G8R8A8_UNORM;
    vk_image_info.fLevelCount = 1;
    GrBackendRenderTarget render_target(width_, height_, 0, 0, vk_image_info);
    sk_surface = SkSurface::MakeFromBackendRenderTarget(
        vk_swapchain_->GetGrContext(), render_target, kTopLeft_GrSurfaceOrigin,
        kBGRA_8888_SkColorType, nullptr, &surface_props);
    FXL_CHECK(sk_surface);
    SkCanvas* canvas = sk_surface->getCanvas();
    canvas->clear(SK_ColorWHITE);
  } else {
    auto backend = sk_surface->getBackendRenderTarget(
        SkSurface::kFlushRead_BackendHandleAccess);
    backend.setVkImageLayout(static_cast<VkImageLayout>(image->layout));
  }
}

void SkiaGpuPainter::SetImageLayout(Swapchain::SwapchainImageResources* image) {
  auto& sk_surface = image_draw_resources_[image->index].sk_surface;
  auto backend = sk_surface->getBackendRenderTarget(
      SkSurface::kFlushRead_BackendHandleAccess);
  GrVkImageInfo vk_image_info;
  if (!backend.getVkImageInfo(&vk_image_info))
    FXL_CHECK(false) << "Failed to get image info";
  image->layout = static_cast<vk::ImageLayout>(vk_image_info.fImageLayout);
}

}  // namespace examples
