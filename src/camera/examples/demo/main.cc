// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
fx shell tiles_ctl add fuchsia-pkg://fuchsia.com/camera_demo#meta/camera_demo.cmx
*/

#include <dirent.h>
#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/fdio.h>
#include <lib/ui/base_view/cpp/base_view.h>
#include <lib/ui/base_view/cpp/view_provider_component.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/zx/time.h>
#include <stream_provider.h>
#include <sys/types.h>
#include <text_node.h>

#include <queue>
#include <random>

#include <fbl/unique_fd.h>

#include "src/lib/component/cpp/startup_context.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/ui/lib/glm_workaround/glm_workaround.h"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

static const uint32_t kChaosMaxSleepMsec = 2000;
static const uint32_t kChaosMeanSleepMsec = 50;

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
      FXL_LOG(ERROR) << "sysmem pixel format (" << static_cast<uint32_t>(type)
                     << ") has no corresponding fuchsia::images::PixelFormat";
      return static_cast<fuchsia::images::PixelFormat>(-1);
  }
}

// Draws a scenic scene containing a single rectangle with an image pipe material,
// constructed with buffers populated by a stream provider.
class DemoView : public scenic::BaseView, public fuchsia::camera2::Stream::EventSender_ {
 public:
  explicit DemoView(scenic::ViewContext context, async::Loop* loop, bool chaos)
      : BaseView(std::move(context), "Camera Demo"),
        loop_(loop),
        chaos_(chaos),
        chaos_dist_(kChaosMaxSleepMsec,
                    static_cast<float>(kChaosMeanSleepMsec) / kChaosMaxSleepMsec),
        node_(session()),
        text_node_(session()) {}

  ~DemoView() override {
    // Manually delete Wait instances before their corresponding events to avoid a failed assert.
    // TODO(36367): revisit async::Wait object lifetime requirements
    while (waiters_.size() > 0) {
      waiters_.front().first = nullptr;
      waiters_.pop();
    }
  };

  static std::unique_ptr<DemoView> Create(scenic::ViewContext context, async::Loop* loop,
                                          bool chaos) {
    auto view = std::make_unique<DemoView>(std::move(context), loop, chaos);

    view->stream_provider_ = StreamProvider::Create(StreamProvider::Source::CONTROLLER);
    if (!view->stream_provider_) {
      FXL_LOG(ERROR) << "Failed to get CONTROLLER stream provider";
      return nullptr;
    }

    fuchsia::sysmem::ImageFormat_2 format;
    fuchsia::sysmem::BufferCollectionInfo_2 buffers;
    zx_status_t status = view->stream_provider_->ConnectToStream(
        view->stream_.NewRequest(loop->dispatcher()), &format, &buffers, &view->should_rotate_);
    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status) << "Failed to connect to stream";
      return nullptr;
    }
    view->stream_.set_error_handler([loop](zx_status_t status) {
      FXL_PLOG(ERROR, status) << "Stream disconnected";
      loop->Quit();
    });
    view->stream_.events().OnFrameAvailable =
        fit::bind_member(view.get(), &DemoView::OnFrameAvailable);

    uint32_t image_pipe_id = view->session()->AllocResourceId();
    view->session()->Enqueue(
        scenic::NewCreateImagePipeCmd(image_pipe_id, view->image_pipe_.NewRequest()));
    scenic::Material material(view->session());
    material.SetTexture(image_pipe_id);
    view->session()->ReleaseResource(image_pipe_id);
    scenic::Rectangle shape(view->session(), format.coded_width, format.coded_height);
    view->shape_width_ = format.coded_width;
    view->shape_height_ = format.coded_height;
    view->node_.SetShape(shape);
    view->node_.SetMaterial(material);
    view->root_node().AddChild(view->node_);
    view->root_node().AddChild(view->text_node_);

    fuchsia::images::ImageInfo image_info{};
    image_info.width = format.coded_width;
    image_info.height = format.coded_height;
    image_info.stride = format.bytes_per_row;
    image_info.pixel_format = convertFormat(format.pixel_format.type);
    for (uint32_t i = 0; i < buffers.buffer_count; ++i) {
      uint32_t image_id = i + 1;  // Scenic doesn't allow clients to use image ID 0.
      view->image_pipe_->AddImage(image_id, image_info, std::move(buffers.buffers[i].vmo), 0,
                                  buffers.settings.buffer_settings.size_bytes,
                                  fuchsia::images::MemoryType::HOST_MEMORY);
      view->image_ids_[i] = image_id;
    }

    view->stream_->Start();

    view->InvalidateScene();

    return view;
  }

 private:
  // |scenic::BaseView|
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override {
    if (!has_logical_size() || !has_metrics())
      return;
    if (should_rotate_) {
      auto rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1));
      node_.SetRotation(rotation.x, rotation.y, rotation.z, rotation.w);
    }
    node_.SetTranslation(logical_size().x * 0.5f, logical_size().y * 0.5f, -1.0f);
    const float shape_vertical_size = should_rotate_ ? shape_width_ : shape_height_;
    const float scale = logical_size().y / shape_vertical_size;  // Fit vertically.
    node_.SetScale(scale, scale, 1.0f);
    text_node_.SetText(stream_provider_->GetName() +
                       (should_rotate_ ? " (Rotated by Scenic)" : ""));
    text_node_.SetTranslation(logical_size().x * 0.5f + metrics().scale_x * 0.5f,
                              logical_size().y * 0.02f, -1.1f);
    text_node_.SetScale(1.0f / metrics().scale_x, 1.0f / metrics().scale_y,
                        1.0f / metrics().scale_z);
  }

  void OnInputEvent(fuchsia::ui::input::InputEvent event) override {
    if (event.is_pointer() && event.pointer().phase == fuchsia::ui::input::PointerEventPhase::UP) {
      loop_->Quit();
    }
  }

  // |scenic::SessionListener|
  void OnScenicError(std::string error) override { FXL_LOG(ERROR) << "Scenic Error " << error; }

  // |fuchsia::camera2::Stream|
  void OnFrameAvailable(fuchsia::camera2::FrameAvailableInfo info) override {
    SleepIfChaos();
    if (!has_logical_size()) {
      stream_->ReleaseFrame(info.buffer_id);
      return;
    }
    if (info.frame_status != fuchsia::camera2::FrameStatus::OK) {
      FXL_LOG(ERROR) << "Received OnFrameAvailable with error event";
      return;
    }

    zx::event release_fence;
    zx_status_t status = zx::event::create(0, &release_fence);
    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status) << "Failed to create fence";
      loop_->Quit();
      return;
    }
    std::vector<zx::event> acquire_fences;
    std::vector<zx::event> release_fences(1);
    status = release_fence.duplicate(ZX_RIGHT_SAME_RIGHTS, &release_fences[0]);
    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status) << "Failed to duplicate fence";
      loop_->Quit();
      return;
    }

    uint64_t now_ns = zx_clock_get_monotonic();
    image_pipe_->PresentImage(
        image_ids_[info.buffer_id], now_ns, std::move(acquire_fences), std::move(release_fences),
        [this](fuchsia::images::PresentationInfo presentation_info) { SleepIfChaos(); });

    auto waiter = std::make_unique<async::Wait>(
        release_fence.get(), ZX_EVENT_SIGNALED, 0,
        [this, buffer_id = info.buffer_id](async_dispatcher_t* dispatcher, async::Wait* wait,
                                           zx_status_t status, const zx_packet_signal_t* signal) {
          if (status != ZX_OK) {
            FXL_PLOG(ERROR, status) << "Wait failed";
            loop_->Quit();
            return;
          }
          SleepIfChaos();
          stream_->ReleaseFrame(buffer_id);
        });
    waiters_.emplace(std::move(waiter), std::move(release_fence));
    status = waiters_.back().first->Begin(loop_->dispatcher());
    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status) << "Failed to start waiter";
      loop_->Quit();
      return;
    }

    // Remove any signaled waiters.
    zx_signals_t signals{};
    while (waiters_.size() > 0 && zx_object_wait_one(waiters_.front().second.get(),
                                                     ZX_EVENT_SIGNALED, 0, &signals) == ZX_OK) {
      waiters_.pop();
    }

    SleepIfChaos();
  }

  void SleepIfChaos() {
    if (!chaos_) {
      return;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(chaos_dist_(chaos_gen_))));
  }

  async::Loop* loop_;
  bool chaos_;
  std::mt19937 chaos_gen_;
  std::binomial_distribution<uint32_t> chaos_dist_;
  fuchsia::camera2::StreamPtr stream_;
  scenic::ShapeNode node_;
  TextNode text_node_;
  fuchsia::images::ImagePipePtr image_pipe_;
  std::map<uint32_t, uint32_t> image_ids_;
  float shape_width_;
  float shape_height_;
  bool should_rotate_;
  std::queue<std::pair<std::unique_ptr<async::Wait>, zx::event>> waiters_;
  std::unique_ptr<StreamProvider> stream_provider_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  // Chaos mode adds random delays between frame acquisition, presentation, and release.
  bool chaos = false;
  if (argc > 1 && std::string(argv[1]) == "--chaos") {
    std::cout << "Chaos mode enabled!" << std::endl;
    chaos = true;
  }
  scenic::ViewProviderComponent component(
      [&loop, chaos](scenic::ViewContext context) {
        return DemoView::Create(std::move(context), &loop, chaos);
      },
      &loop);
  FXL_LOG(INFO) << argv[0] << " initialized successfully - entering loop";
  zx_status_t status = loop.Run();
  if (status != ZX_ERR_CANCELED) {
    FXL_LOG(WARNING) << "Main thread terminated abnormally";
    return status == ZX_OK ? -1 : status;
  }
  return 0;
}
