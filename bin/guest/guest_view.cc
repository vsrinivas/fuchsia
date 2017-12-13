// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/guest_view.h"

#include "lib/fsl/tasks/message_loop.h"
#include "lib/ui/view_framework/view_provider_app.h"

// For now we expose a fixed size display to the guest. Scenic will scale this
// buffer to the actual window size on the host.
static constexpr uint32_t kDisplayWidth = 1024;
static constexpr uint32_t kDisplayHeight = 768;

ScenicScanout::ScenicScanout(GuestView* view) {}

void ScenicScanout::FlushRegion(const virtio_gpu_rect_t& rect) {
  GpuScanout::FlushRegion(rect);
}

static int view_task(void* ctx) {
  fsl::MessageLoop loop;

  mozart::ViewProviderApp app([ctx](mozart::ViewContext view_context) {
    return std::make_unique<GuestView>(
        reinterpret_cast<machina::VirtioGpu*>(ctx),
        std::move(view_context.view_manager),
        std::move(view_context.view_owner_request));
  });

  loop.Run();
  return 0;
}

// static
zx_status_t GuestView::Start(machina::VirtioGpu* gpu) {
  thrd_t thread;
  int ret = thrd_create(&thread, view_task, gpu);
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  ret = thrd_detach(thread);
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

GuestView::GuestView(
    machina::VirtioGpu* gpu,
    mozart::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
    : BaseView(std::move(view_manager), std::move(view_owner_request), "Guest"),
      background_node_(session()),
      material_(session()),
      scanout_(this) {
  background_node_.SetMaterial(material_);
  parent_node().AddChild(background_node_);

  image_info_.width = kDisplayWidth;
  image_info_.height = kDisplayHeight;
  image_info_.stride = kDisplayWidth * 4;
  image_info_.pixel_format = scenic::ImageInfo::PixelFormat::BGRA_8;
  image_info_.color_space = scenic::ImageInfo::ColorSpace::SRGB;
  image_info_.tiling = scenic::ImageInfo::Tiling::LINEAR;

  // Allocate a framebuffer and attach it as a GPU scanout.
  memory_ = fbl::make_unique<scenic_lib::HostMemory>(
      session(), scenic_lib::Image::ComputeSize(image_info_));
  machina::GpuBitmap bitmap(kDisplayWidth, kDisplayHeight,
                            reinterpret_cast<uint8_t*>(memory_->data_ptr()));
  scanout_.SetBitmap(std::move(bitmap));
  gpu->AddScanout(&scanout_);
}

GuestView::~GuestView() = default;

void GuestView::OnSceneInvalidated(
    scenic::PresentationInfoPtr presentation_info) {
  if (!has_logical_size())
    return;

  const uint32_t width = logical_size().width;
  const uint32_t height = logical_size().height;
  scenic_lib::Rectangle background_shape(session(), width, height);
  background_node_.SetShape(background_shape);

  static constexpr float kBackgroundElevation = 0.f;
  const float center_x = width * .5f;
  const float center_y = height * .5f;
  background_node_.SetTranslation(center_x, center_y, kBackgroundElevation);

  scenic_lib::HostImage image(*memory_, 0u, image_info_.Clone());
  material_.SetTexture(image);

  // TODO(MZ-403): Move this into ScenicScanout::FlushRegion.
  InvalidateScene();
}
