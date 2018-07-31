// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "yuv_view.h"

#include <lib/fxl/log_level.h>
#include <lib/fxl/logging.h>
#include <lib/images/images_util.h>
#include <lib/images/yuv_util.h>
#include <lib/ui/scenic/fidl_helpers.h>

#include <iostream>

namespace {

constexpr uint32_t kShapeWidth = 640;
constexpr uint32_t kShapeHeight = 480;
constexpr float kDisplayHeight = 50;
constexpr float kInitialWindowXPos = 320;
constexpr float kInitialWindowYPos = 240;

}  // namespace

YuvView::YuvView(async::Loop* loop, component::StartupContext* startup_context,
                 ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
                 fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
                     view_owner_request,
                 fuchsia::images::PixelFormat pixel_format)
    : BaseView(std::move(view_manager), std::move(view_owner_request),
               "YuvView Example"),
      node_(session()),
      pixel_format_(pixel_format),
      stride_(static_cast<uint32_t>(
          (kShapeWidth * images_util::BitsPerPixel(pixel_format_) + 7) / 8)) {
  FXL_VLOG(4) << "Creating View";
  // Create an ImagePipe and use it.
  uint32_t image_pipe_id = session()->AllocResourceId();
  session()->Enqueue(scenic::NewCreateImagePipeCmd(
      image_pipe_id, image_pipe_.NewRequest(loop->dispatcher())));

  // Create a material that has our image pipe mapped onto it:
  scenic::Material material(session());
  material.SetTexture(image_pipe_id);
  session()->ReleaseResource(image_pipe_id);

  // Create a rectangle shape to display the YUV on.
  scenic::Rectangle shape(session(), kShapeWidth, kShapeHeight);

  node_.SetShape(shape);
  node_.SetMaterial(material);
  parent_node().AddChild(node_);

  // Translation of 0, 0 is the middle of the screen
  node_.SetTranslation(kInitialWindowXPos, kInitialWindowYPos, kDisplayHeight);
  InvalidateScene();

  StartYuv();
}

YuvView::~YuvView() = default;

void YuvView::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size()) {
    return;
  }

  // Compute the amount of time that has elapsed since the view was created.
  double seconds =
      static_cast<double>(presentation_info.presentation_time) / 1'000'000'000;

  const float kHalfWidth = logical_size().width * 0.5f;
  const float kHalfHeight = logical_size().height * 0.5f;

  // Compute the translation for the window to swirl around the screen.
  // Why do this?  Well, this is an example of what a View can do, and it helps
  // debug to know if scenic is still running.
  node_.SetTranslation(kHalfWidth * (1. + .1 * sin(seconds * 0.8)),
                       kHalfHeight * (1. + .1 * sin(seconds * 0.6)),
                       kDisplayHeight);

  // The recangle is constantly animating; invoke InvalidateScene() to guarantee
  // that OnSceneInvalidated() will be called again.
  InvalidateScene();
}

void YuvView::StartYuv() {
  constexpr uint32_t kImageId = 1;
  fuchsia::images::ImageInfo image_info{
      .width = kShapeWidth,
      .height = kShapeHeight,
      .stride = stride_,
      .pixel_format = pixel_format_,
  };

  uint64_t image_vmo_bytes = images_util::ImageSize(image_info);

  ::zx::vmo image_vmo;
  zx_status_t status = ::zx::vmo::create(image_vmo_bytes, 0, &image_vmo);
  if (status != ZX_OK) {
    FXL_LOG(FATAL) << "::zx::vmo::create() failed";
    FXL_NOTREACHED();
  }

  uint8_t* vmo_base;
  status =
      zx::vmar::root_self()->map(0, image_vmo, 0, image_vmo_bytes,
                                 ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_READ,
                                 reinterpret_cast<uintptr_t*>(&vmo_base));

  SetVmoPixels(vmo_base);

  constexpr uint64_t kMemoryOffset = 0;

  image_pipe_->AddImage(kImageId, image_info, std::move(image_vmo),
                        fuchsia::images::MemoryType::HOST_MEMORY,
                        kMemoryOffset);

  ::fidl::VectorPtr<::zx::event> acquire_fences =
      ::fidl::VectorPtr<::zx::event>::New(0);
  ::fidl::VectorPtr<::zx::event> release_fences =
      ::fidl::VectorPtr<::zx::event>::New(0);
  uint64_t now_ns = zx_clock_get(ZX_CLOCK_MONOTONIC);
  image_pipe_->PresentImage(
      kImageId, now_ns, std::move(acquire_fences), std::move(release_fences),
      [this](fuchsia::images::PresentationInfo presentation_info) {
        std::cout << "PresentImageCallback() called" << std::endl;
      });
}

void YuvView::SetVmoPixels(uint8_t* vmo_base) {
  switch (pixel_format_) {
    case fuchsia::images::PixelFormat::BGRA_8:
      SetBgra8Pixels(vmo_base);
      break;
    case fuchsia::images::PixelFormat::YUY2:
      SetYuy2Pixels(vmo_base);
      break;
    case fuchsia::images::PixelFormat::NV12:
      SetNv12Pixels(vmo_base);
      break;
  }
}

void YuvView::SetBgra8Pixels(uint8_t* vmo_base) {
  for (uint32_t y_iter = 0; y_iter < kShapeHeight; y_iter++) {
    double y = static_cast<double>(y_iter) / kShapeHeight;
    for (uint32_t x_iter = 0; x_iter < kShapeWidth; x_iter++) {
      double x = static_cast<double>(x_iter) / kShapeWidth;
      uint8_t y_value = GetYValue(x, y) * 255;
      uint8_t u_value = GetUValue(x, y) * 255;
      uint8_t v_value = GetVValue(x, y) * 255;
      yuv_util::YuvToBgra(
          y_value, u_value, v_value,
          &vmo_base[y_iter * stride_ + x_iter * sizeof(uint32_t)]);
    }
  }
}

void YuvView::SetYuy2Pixels(uint8_t* vmo_base) {
  for (uint32_t y_iter = 0; y_iter < kShapeHeight; y_iter++) {
    double y = static_cast<double>(y_iter) / kShapeHeight;
    for (uint32_t x_iter = 0; x_iter < kShapeWidth; x_iter += 2) {
      double x0 = static_cast<double>(x_iter) / kShapeWidth;
      double x1 = static_cast<double>(x_iter + 1) / kShapeWidth;
      uint8_t* two_pixels =
          &vmo_base[y_iter * stride_ + x_iter * sizeof(uint16_t)];
      two_pixels[0] = GetYValue(x0, y) * 255;
      two_pixels[1] = GetUValue(x0, y) * 255;
      two_pixels[2] = GetYValue(x1, y) * 255;
      two_pixels[3] = GetVValue(x0, y) * 255;
    }
  }
}

void YuvView::SetNv12Pixels(uint8_t* vmo_base) {
  // Y plane
  uint8_t* y_base = vmo_base;
  for (uint32_t y_iter = 0; y_iter < kShapeHeight; y_iter++) {
    double y = static_cast<double>(y_iter) / kShapeHeight;
    for (uint32_t x_iter = 0; x_iter < kShapeWidth; x_iter++) {
      double x = static_cast<double>(x_iter) / kShapeWidth;
      y_base[y_iter * stride_ + x_iter] = GetYValue(x, y) * 255;
    }
  }
  // UV interleaved
  uint8_t* uv_base = y_base + kShapeHeight * stride_;
  for (uint32_t y_iter = 0; y_iter < kShapeHeight / 2; y_iter++) {
    double y = static_cast<double>(y_iter * 2) / kShapeHeight;
    for (uint32_t x_iter = 0; x_iter < kShapeWidth / 2; x_iter++) {
      double x = static_cast<double>(x_iter * 2) / kShapeWidth;
      uv_base[y_iter * stride_ + x_iter * 2] = GetUValue(x, y) * 255;
      uv_base[y_iter * stride_ + x_iter * 2 + 1] = GetVValue(x, y) * 255;
    }
  }
}

double YuvView::GetYValue(double x, double y) { return x; }

double YuvView::GetUValue(double x, double y) { return y; }

double YuvView::GetVValue(double x, double y) { return 1 - y; }
