// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/examples/camera-gym/display_view.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>  // PostTask
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/zx/time.h>
#include <sys/types.h>
#include <zircon/errors.h>

#include <sstream>

#include <src/lib/syslog/cpp/logger.h>

#include "src/camera/examples/camera_display/text_node/text_node.h"

namespace camera {

DisplayView::DisplayView(scenic::ViewContext context, async::Loop* loop)
    : BaseView(std::move(context), "Camera Gym"),
      loop_(loop),
      text_node_(session()),
      trace_provider_(loop_->dispatcher()) {}

DisplayView::~DisplayView() {
  for (auto& ipp: image_pipe_properties_) {
    for (auto& waiter : ipp.waiters) {
      waiter.second.first->Cancel();
    }
  }
}

std::unique_ptr<DisplayView> DisplayView::Create(scenic::ViewContext context, async::Loop* loop) {
  auto view = std::make_unique<DisplayView>(std::move(context), loop);
  ZX_ASSERT(view->Initialize() == ZX_OK);
  ZX_ASSERT(view->RunOneSession() == ZX_OK);
  return view;
}

zx_status_t DisplayView::Initialize() {
  stream_provider_ = camera::StreamProvider::Create(loop_);
  zx_status_t status = stream_provider_->Initialize();
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "StreamProvider Initialize";
    return status;
  }
  return ZX_OK;
}

zx_status_t DisplayView::RunOneSession() {
  total_width_ = 0.0f;

  uint32_t kStreamCount = 1;  // TODO(48506) - Need to fix.
  for (uint32_t stream_index = 0; stream_index < kStreamCount; ++stream_index) {
    auto result = stream_provider_->ConnectToStream(

        // TODO(48124) - camera2 going away
        [this, stream_index](fuchsia::camera2::FrameAvailableInfo info) {
          OnFrameAvailable(stream_index, std::move(info));
        });
    if (result.is_error()) {
      if (result.error() == ZX_ERR_OUT_OF_RANGE) {
        break;
      }
      FX_PLOGS(ERROR, result.error()) << "Failed to connect to stream";
      return ZX_ERR_INTERNAL;
    }
    auto& ipp = image_pipe_properties_.emplace_back(session());
    auto [format, buffers] = result.take_value();
    uint32_t image_pipe_id = session()->AllocResourceId();
    session()->Enqueue(scenic::NewCreateImagePipeCmd(
        image_pipe_id, ipp.image_pipe.NewRequest(loop_->dispatcher())));
    scenic::Material material(session());
    material.SetTexture(image_pipe_id);
    session()->ReleaseResource(image_pipe_id);
    scenic::Rectangle shape(session(), format.coded_width, format.coded_height);
    ipp.shape_width = format.coded_width;
    ipp.shape_height = format.coded_height;
    total_width_ += ipp.shape_width;
    max_height_ = std::max(max_height_, ipp.shape_height);
    ipp.node.SetShape(shape);
    ipp.node.SetMaterial(material);
    root_node().AddChild(ipp.node);

    fuchsia::images::ImageInfo image_info{};
    image_info.width = format.coded_width;
    image_info.height = format.coded_height;
    image_info.stride = format.bytes_per_row;

    FX_LOGS(INFO) << "width=" << image_info.width;
    FX_LOGS(INFO) << "height=" << image_info.height;
    FX_LOGS(INFO) << "stride=" << image_info.stride;

    ZX_ASSERT(format.pixel_format.type == fuchsia::sysmem::PixelFormatType::NV12);
    image_info.pixel_format = fuchsia::images::PixelFormat::NV12;
    for (uint32_t i = 0; i < buffers.buffer_count; ++i) {
      uint32_t image_id = i + 1;  // Scenic doesn't allow clients to use image ID 0.
      ipp.image_pipe->AddImage(image_id, image_info, std::move(buffers.buffers[i].vmo), 0,
                               buffers.settings.buffer_settings.size_bytes,
                               fuchsia::images::MemoryType::HOST_MEMORY);
      ipp.image_ids[i] = image_id;
    }

    // Kick start the stream
    stream_provider_->PostGetNextFrame();
  }
  InvalidateScene();

  return ZX_OK;
}

void DisplayView::SetDescriptionEnabled(bool enabled) {
  if (enabled) {
    root_node().AddChild(text_node_);
  } else {
    text_node_.Detach();
  }
}

void DisplayView::OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size() || !has_metrics()) {
    return;
  }

  // Determine a uniform scale factor that will allow all nodes to be visible.
  constexpr float kPadding = 8.0f;
  const float content_width = total_width_ + kPadding * (image_pipe_properties_.size() - 1);
  const float content_height = max_height_;
  float scale_x = logical_size().x / content_width;
  float scale_y = logical_size().y / content_height;
  float scale = std::min(scale_x, scale_y);
  float offset_x = logical_size().x * 0.5f - content_width * scale * 0.5f;
  for (auto& ipp : image_pipe_properties_) {
    ipp.node.SetScale(scale, scale, 1.0f);
    ipp.node.SetTranslation(offset_x + ipp.shape_width * scale * 0.5f, logical_size().y * 0.5f,
                            -1.0f);
    offset_x += (ipp.shape_width + kPadding) * scale;
  }
  text_node_.SetTranslation(logical_size().x * 0.5f + metrics().scale_x * 0.5f,
                            logical_size().y * 0.02f, -1.1f);

  std::stringstream ss;
  ss << "CURRENT CONFIGURATION";
  text_node_.SetText(stream_provider_->GetName() + ss.str());
  text_node_.SetScale(1.0f / metrics().scale_x, 1.0f / metrics().scale_y, 1.0f / metrics().scale_z);
}

void DisplayView::OnInputEvent(fuchsia::ui::input::InputEvent event) {
  if (event.is_pointer() && event.pointer().phase == fuchsia::ui::input::PointerEventPhase::UP) {
    loop_->Quit();
  }
}

void DisplayView::OnScenicError(std::string error) { FX_LOGS(ERROR) << "Scenic Error " << error; }

// TODO(48124) - camera2 going away
void DisplayView::OnFrameAvailable(uint32_t stream_index,
                                   fuchsia::camera2::FrameAvailableInfo info) {
  auto async_id = TRACE_NONCE();
  TRACE_ASYNC_BEGIN("camera", "FrameHeld", async_id, "buffer_id", info.buffer_id);
  auto& ipp = image_pipe_properties_[stream_index];
  if (!has_logical_size()) {
    stream_provider_->ReleaseFrame(info.buffer_id);
    TRACE_ASYNC_END("camera", "FrameHeld", async_id, "buffer_id", info.buffer_id);
    return;
  }

  // TODO(48124) - camera2 going away
  if (info.frame_status != fuchsia::camera2::FrameStatus::OK) {
    FX_LOGS(ERROR) << "Received OnFrameAvailable with error event";
    return;
  }

  // release_fence is used to ensure that all Scenic consumers are done with this buffer ID.
  zx::event release_fence;
  zx_status_t status = zx::event::create(0, &release_fence);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create fence";
    loop_->Quit();
    return;
  }
  std::vector<zx::event> acquire_fences;
  std::vector<zx::event> release_fences(1);
  status = release_fence.duplicate(ZX_RIGHT_SAME_RIGHTS, &release_fences[0]);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to duplicate fence";
    loop_->Quit();
    return;
  }

  uint64_t now_ns = zx_clock_get_monotonic();
  ipp.image_pipe->PresentImage(ipp.image_ids[info.buffer_id], now_ns, std::move(acquire_fences),
                               std::move(release_fences),
                               [](fuchsia::images::PresentationInfo presentation_info) {});

  // Wait for release_fence to be released.
  auto waiter = std::make_unique<async::Wait>(
      release_fence.get(), ZX_EVENT_SIGNALED, 0,
      [this, async_id, buffer_id = info.buffer_id, &ipp](async_dispatcher_t* dispatcher,
                                                         async::Wait* wait, zx_status_t status,
                                                         const zx_packet_signal_t* signal) {
        if (status != ZX_OK) {
          FX_PLOGS(ERROR, status) << "Wait failed";
          loop_->Quit();
          return;
        }

        // Release buffer ID back to source.
        stream_provider_->ReleaseFrame(buffer_id);

        TRACE_ASYNC_END("camera", "FrameHeld", async_id, "buffer_id", buffer_id);
        auto it = ipp.waiters.find(buffer_id);
        ZX_ASSERT(it != ipp.waiters.end());
        it->second.first = nullptr;
        ipp.waiters.erase(it);
      });
  auto it = ipp.waiters.emplace(
      std::make_pair(info.buffer_id, std::make_pair(std::move(waiter), std::move(release_fence))));
  status = it.first->second.first->Begin(loop_->dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to start waiter";
    loop_->Quit();
    return;
  }
}

}  // namespace camera
