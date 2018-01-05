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

ScenicScanout::ScenicScanout(GuestView* view)
    : view_(view),
      task_runner_(fsl::MessageLoop::GetCurrent()->task_runner()) {}

void ScenicScanout::FlushRegion(const virtio_gpu_rect_t& rect) {
  GpuScanout::FlushRegion(rect);
  task_runner_->PostTask([this] { view_->InvalidateScene(); });
}

struct ViewTaskArgs {
  machina::VirtioGpu* gpu;
  machina::InputDispatcher* input_dispatcher;
};

static int view_task(void* ctx) {
  ViewTaskArgs* args = reinterpret_cast<ViewTaskArgs*>(ctx);

  fsl::MessageLoop loop;
  mozart::ViewProviderApp app([args](mozart::ViewContext view_context) {
    auto view = std::make_unique<GuestView>(
        args->gpu, args->input_dispatcher, std::move(view_context.view_manager),
        std::move(view_context.view_owner_request));

    delete args;
    return view;
  });

  loop.Run();
  return 0;
}

// static
zx_status_t GuestView::Start(machina::VirtioGpu* gpu,
                             machina::InputDispatcher* input_dispatcher) {
  ViewTaskArgs* args = new ViewTaskArgs;
  args->gpu = gpu;
  args->input_dispatcher = input_dispatcher;

  thrd_t thread;
  int ret = thrd_create(&thread, view_task, args);
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
    machina::InputDispatcher* input_dispatcher,
    mozart::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
    : BaseView(std::move(view_manager), std::move(view_owner_request), "Guest"),
      background_node_(session()),
      material_(session()),
      scanout_(this),
      input_dispatcher_(input_dispatcher) {
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
}

bool GuestView::OnInputEvent(mozart::InputEventPtr event) {
  if (event->is_keyboard()) {
    const mozart::KeyboardEventPtr& key_event = event->get_keyboard();

    machina::InputEvent event;
    event.type = machina::InputEventType::KEYBOARD;
    event.key.hid_usage = key_event->hid_usage;
    switch (key_event->phase) {
      case mozart::KeyboardEvent::Phase::PRESSED:
        event.key.state = machina::KeyState::PRESSED;
        break;
      case mozart::KeyboardEvent::Phase::RELEASED:
      case mozart::KeyboardEvent::Phase::CANCELLED:
        event.key.state = machina::KeyState::RELEASED;
        break;
      default:
        // Ignore events for unsupported phases.
        return true;
    }
    input_dispatcher_->PostEvent(event, true);
    return true;
  }
  return false;
}
