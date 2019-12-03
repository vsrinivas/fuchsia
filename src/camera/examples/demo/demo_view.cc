// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "demo_view.h"

#include <dirent.h>
#include <fcntl.h>
#include <lib/fdio/fdio.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/zx/time.h>
#include <sys/types.h>
#include <zircon/errors.h>

#include <queue>
#include <random>

#include <fbl/unique_fd.h>

#include "src/camera/stream_utils/image_io_util.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/lib/glm_workaround/glm_workaround.h"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

namespace camera {

static constexpr uint32_t kChaosMaxSleepMsec = 2000;
static constexpr uint32_t kChaosMeanSleepMsec = 50;

static fuchsia::images::PixelFormat convertFormat(fuchsia::sysmem::PixelFormatType type) {
  switch (type) {
    case fuchsia::sysmem::PixelFormatType::BGRA32:
      return fuchsia::images::PixelFormat::BGRA_8;
    case fuchsia::sysmem::PixelFormatType::YUY2:
      return fuchsia::images::PixelFormat::YUY2;
    case fuchsia::sysmem::PixelFormatType::NV12:
      return fuchsia::images::PixelFormat::NV12;
    case fuchsia::sysmem::PixelFormatType::YV12:
      return fuchsia::images::PixelFormat::YV12;
    default:
      FX_LOGS(ERROR) << "sysmem pixel format (" << static_cast<uint32_t>(type)
                     << ") has no corresponding fuchsia::images::PixelFormat";
      return static_cast<fuchsia::images::PixelFormat>(-1);
  }
}

DemoView::DemoView(scenic::ViewContext context, async::Loop* loop, bool chaos, bool image_io)
    : BaseView(std::move(context), "Camera Demo"),
      loop_(loop),
      chaos_(chaos),
      chaos_dist_(kChaosMaxSleepMsec, static_cast<float>(kChaosMeanSleepMsec) / kChaosMaxSleepMsec),
      text_node_(session()),
      image_io_(image_io),
      trace_provider_(loop->dispatcher()) {}

DemoView::~DemoView() {
  // Manually delete Wait instances before their corresponding events to avoid a failed assert.
  // TODO(36367): revisit async::Wait object lifetime requirements
  for (auto& ipp : image_pipe_properties_) {
    for (auto& waiter : ipp.waiters) {
      waiter.second.first = nullptr;
    }
  }
};

std::unique_ptr<DemoView> DemoView::Create(scenic::ViewContext context, async::Loop* loop,
                                           bool chaos, bool image_io) {
  auto view = std::make_unique<DemoView>(std::move(context), loop, chaos, image_io);

  view->stream_provider_ = StreamProvider::Create(StreamProvider::Source::MANAGER);
  if (!view->stream_provider_) {
    FX_LOGS(ERROR) << "Failed to get MANAGER stream provider";
    return nullptr;
  }

  view->total_width_ = 0.0f;
  for (uint32_t index = 0;; ++index) {
    fuchsia::camera2::StreamPtr stream;
    auto result =
        view->stream_provider_->ConnectToStream(stream.NewRequest(loop->dispatcher()), index);
    if (result.is_error()) {
      if (result.error() == ZX_ERR_OUT_OF_RANGE) {
        break;
      }
      FX_PLOGS(ERROR, result.error()) << "Failed to connect to stream";
      return nullptr;
    }
    auto& ipp = view->image_pipe_properties_.emplace_back(view->session());
    ipp.stream = std::move(stream);
    auto [format, buffers, should_rotate] = result.take_value();
    ipp.should_rotate = should_rotate;
    ipp.stream.set_error_handler([loop](zx_status_t status) {
      FX_PLOGS(ERROR, status) << "Stream disconnected";
      loop->Quit();
    });
    ipp.stream.events().OnFrameAvailable =
        [index, view = view.get()](fuchsia::camera2::FrameAvailableInfo info) {
          view->OnFrameAvailable(index, std::move(info));
        };

    std::stringstream ss;
    ss << "/demo/" << index;
    ipp.image_io_util = camera::ImageIOUtil::Create(&buffers, ss.str());
    if (ipp.image_io_util) {
      FX_LOGS(ERROR) << "Failed to create ImageIOUtil";
    } else {
      FX_LOGS(INFO) << "ImageIOUtil Created!";
    }

    uint32_t image_pipe_id = view->session()->AllocResourceId();
    view->session()->Enqueue(scenic::NewCreateImagePipeCmd(
        image_pipe_id, ipp.image_pipe.NewRequest(loop->dispatcher())));
    scenic::Material material(view->session());
    material.SetTexture(image_pipe_id);
    view->session()->ReleaseResource(image_pipe_id);
    scenic::Rectangle shape(view->session(), format.coded_width, format.coded_height);
    ipp.shape_width = should_rotate ? format.coded_height : format.coded_width;
    ipp.shape_height = should_rotate ? format.coded_width : format.coded_height;
    view->total_width_ += ipp.shape_width;
    view->max_height_ = std::max(view->max_height_, ipp.shape_height);
    ipp.node.SetShape(shape);
    ipp.node.SetMaterial(material);
    view->root_node().AddChild(ipp.node);

    fuchsia::images::ImageInfo image_info{};
    image_info.width = format.coded_width;
    image_info.height = format.coded_height;
    image_info.stride = format.bytes_per_row;
    image_info.pixel_format = convertFormat(format.pixel_format.type);
    for (uint32_t i = 0; i < buffers.buffer_count; ++i) {
      uint32_t image_id = i + 1;  // Scenic doesn't allow clients to use image ID 0.
      ipp.image_pipe->AddImage(image_id, image_info, std::move(buffers.buffers[i].vmo), 0,
                               buffers.settings.buffer_settings.size_bytes,
                               fuchsia::images::MemoryType::HOST_MEMORY);
      ipp.image_ids[i] = image_id;
    }

    ipp.stream->Start();
  }

  view->root_node().AddChild(view->text_node_);
  view->InvalidateScene();

  return view;
}

void DemoView::OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size() || !has_metrics()) {
    return;
  }

  uint32_t rotated_count = 0;

  // Determine a uniform scale factor that will allow all nodes to be visible.
  constexpr float kPadding = 8.0f;
  const float content_width = total_width_ + kPadding * (image_pipe_properties_.size() - 1);
  const float content_height = max_height_;
  float scale_x = logical_size().x / content_width;
  float scale_y = logical_size().y / content_height;
  float scale = std::min(scale_x, scale_y);

  float offset_x = logical_size().x * 0.5f - content_width * scale * 0.5f;
  for (auto& ipp : image_pipe_properties_) {
    if (ipp.should_rotate) {
      ++rotated_count;
      auto rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1));
      ipp.node.SetRotation(rotation.x, rotation.y, rotation.z, rotation.w);
    }
    ipp.node.SetScale(scale, scale, 1.0f);
    ipp.node.SetTranslation(offset_x + ipp.shape_width * scale * 0.5f, logical_size().y * 0.5f,
                            -1.0f);
    offset_x += (ipp.shape_width + kPadding) * scale;
  }
  text_node_.SetTranslation(logical_size().x * 0.5f + metrics().scale_x * 0.5f,
                            logical_size().y * 0.02f, -1.1f);

  std::stringstream ss;
  if (rotated_count > 0) {
    ss << " (" << rotated_count << " of " << image_pipe_properties_.size()
       << " nodes rotated by Scenic)";
  }
  text_node_.SetText(stream_provider_->GetName() + ss.str());
  text_node_.SetScale(1.0f / metrics().scale_x, 1.0f / metrics().scale_y, 1.0f / metrics().scale_z);
}

void DemoView::OnInputEvent(fuchsia::ui::input::InputEvent event) {
  if (event.is_pointer() && event.pointer().phase == fuchsia::ui::input::PointerEventPhase::UP) {
    loop_->Quit();
  }
}

void DemoView::OnScenicError(std::string error) { FX_LOGS(ERROR) << "Scenic Error " << error; }

void DemoView::OnFrameAvailable(uint32_t index, fuchsia::camera2::FrameAvailableInfo info) {
  auto async_id = TRACE_NONCE();
  TRACE_ASYNC_BEGIN("camera", "FrameHeld", async_id, "buffer_id", info.buffer_id);
  SleepIfChaos();
  auto& ipp = image_pipe_properties_[index];
  if (!has_logical_size()) {
    ipp.stream->ReleaseFrame(info.buffer_id);
    TRACE_ASYNC_END("camera", "FrameHeld", async_id, "buffer_id", info.buffer_id);
    return;
  }
  if (info.frame_status != fuchsia::camera2::FrameStatus::OK) {
    FX_LOGS(ERROR) << "Received OnFrameAvailable with error event";
    return;
  }

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

  // If flag is enabled, write frame to disk once.
  if (ipp.image_io_util && image_io_) {
    status = ipp.image_io_util->WriteImageData(info.buffer_id);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to write to disk";
    }
    FX_LOGS(INFO) << "Image written to disk";
    image_io_ = false;
  }

  uint64_t now_ns = zx_clock_get_monotonic();
  ipp.image_pipe->PresentImage(
      ipp.image_ids[info.buffer_id], now_ns, std::move(acquire_fences), std::move(release_fences),
      [this](fuchsia::images::PresentationInfo presentation_info) { SleepIfChaos(); });

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
        SleepIfChaos();
        ipp.stream->ReleaseFrame(buffer_id);
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

  SleepIfChaos();
}

void DemoView::SleepIfChaos() {
  if (!chaos_) {
    return;
  }
  zx_nanosleep(zx_deadline_after(ZX_MSEC(chaos_dist_(chaos_gen_))));
}

}  // namespace camera
