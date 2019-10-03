// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
fx shell tiles_ctl add fuchsia-pkg://fuchsia.com/camera_demo#meta/camera_demo.cmx
*/

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/camera/common/cpp/fidl.h>
#include <fuchsia/camera/test/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fdio/fdio.h>
#include <lib/ui/base_view/cpp/base_view.h>
#include <lib/ui/base_view/cpp/view_provider_component.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/zx/time.h>
#include <sys/types.h>

#include <queue>
#include <random>

#include <fbl/unique_fd.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>

#include "src/ui/lib/glm_workaround/glm_workaround.h"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

static constexpr const char* kDevicePath = "/dev/class/isp-device-test/000";

static const uint32_t kChaosMaxSleepMsec = 2000;
static const uint32_t kChaosMeanSleepMsec = 50;

static fuchsia::images::PixelFormat convertFormat(fuchsia_sysmem_PixelFormatType type) {
  switch (type) {
    case fuchsia_sysmem_PixelFormatType_BGRA32:
      return fuchsia::images::PixelFormat::BGRA_8;
    case fuchsia_sysmem_PixelFormatType_YUY2:
      return fuchsia::images::PixelFormat::YUY2;
    case fuchsia_sysmem_PixelFormatType_NV12:
      return fuchsia::images::PixelFormat::NV12;
    case fuchsia_sysmem_PixelFormatType_YV12:
      return fuchsia::images::PixelFormat::YV12;
    default:
      FXL_LOG(ERROR) << "sysmem pixel format (" << type
                     << ") has no corresponding fuchsia::images::PixelFormat";
      return static_cast<fuchsia::images::PixelFormat>(-1);
  }
}

// Draws a scenic scene containing a single rectangle with an image pipe material,
// constructed with buffers populated by a stream provider (in this case, the ArmIspDevice).
class DemoView : public scenic::BaseView {
 public:
  explicit DemoView(scenic::ViewContext context, async::Loop* loop, bool chaos)
      : BaseView(std::move(context), "Camera Demo"),
        loop_(loop),
        chaos_(chaos),
        chaos_dist_(kChaosMaxSleepMsec,
                    static_cast<float>(kChaosMeanSleepMsec) / kChaosMaxSleepMsec),
        node_(session()) {}

  ~DemoView() override {
    // Manually delete Wait instances before their corresponding events to avoid a failed assert.
    // TODO(36367): revisit async::Wait object lifetime requirements
    while (waiters_.size() > 0) {
      waiters_.front().first.reset();
      waiters_.pop();
    }
  };

  static std::unique_ptr<DemoView> Create(scenic::ViewContext context, async::Loop* loop,
                                          bool chaos) {
    auto view = std::make_unique<DemoView>(std::move(context), loop, chaos);

    int result = open(kDevicePath, O_RDONLY);
    if (result < 0) {
      FXL_LOG(ERROR) << "Error opening " << kDevicePath;
      return nullptr;
    }
    fbl::unique_fd isp_fd(result);

    zx::handle isp_handle;
    zx_status_t status = fdio_get_service_handle(isp_fd.get(), isp_handle.reset_and_get_address());
    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status) << "Failed to get service handle";
      return nullptr;
    }

    fuchsia_sysmem_BufferCollectionInfo buffers;
    status = fuchsia_camera_test_IspTesterCreateStream(
        isp_handle.get(), view->stream_.NewRequest().TakeChannel().release(), &buffers);
    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status) << "Failed to call CreateStream";
      return nullptr;
    }

    uint32_t image_pipe_id = view->session()->AllocResourceId();
    view->session()->Enqueue(
        scenic::NewCreateImagePipeCmd(image_pipe_id, view->image_pipe_.NewRequest()));
    scenic::Material material(view->session());
    material.SetTexture(image_pipe_id);
    view->session()->ReleaseResource(image_pipe_id);
    scenic::Rectangle shape(view->session(), buffers.format.image.width,
                            buffers.format.image.height);
    view->shape_width_ = buffers.format.image.width;
    view->node_.SetShape(shape);
    view->node_.SetMaterial(material);
    view->root_node().AddChild(view->node_);

    fuchsia::images::ImageInfo image_info{};
    image_info.width = buffers.format.image.width;
    image_info.height = buffers.format.image.height;
    image_info.stride = buffers.format.image.planes[0].bytes_per_row;
    image_info.pixel_format = convertFormat(buffers.format.image.pixel_format.type);
    for (uint32_t i = 0; i < buffers.buffer_count; ++i) {
      uint32_t image_id = i + 1;  // Scenic doesn't allow clients to use image ID 0.
      zx::vmo vmo(buffers.vmos[i]);
      view->image_pipe_->AddImage(image_id, image_info, std::move(vmo), 0, buffers.vmo_size,
                                  fuchsia::images::MemoryType::HOST_MEMORY);
      view->image_ids_[i] = image_id;
    }

    view->stream_.events().OnFrameAvailable =
        fit::bind_member(view.get(), &DemoView::OnFrameAvailable);
    view->stream_->Start();

    view->InvalidateScene();

    return view;
  }

 private:
  // |scenic::BaseView|
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override {
    if (!has_logical_size())
      return;
    auto rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1));
    node_.SetRotation(rotation.x, rotation.y, rotation.z, rotation.w);
    node_.SetTranslation(logical_size().x * 0.5f, logical_size().y * 0.5f, -5.0f);
    const float scale = logical_size().y / shape_width_;  // Fit portrait vertically after rotation.
    node_.SetScale(scale, scale, 1.0f);
  }

  void OnInputEvent(fuchsia::ui::input::InputEvent event) override {
    if (event.is_pointer() && event.pointer().phase == fuchsia::ui::input::PointerEventPhase::UP) {
      loop_->Quit();
    }
  }

  // |scenic::SessionListener|
  void OnScenicError(std::string error) override { FXL_LOG(ERROR) << "Scenic Error " << error; }

  void OnFrameAvailable(fuchsia::camera::common::FrameAvailableEvent event) {
    SleepIfChaos();
    if (!has_logical_size()) {
      stream_->ReleaseFrame(event.buffer_id);
      return;
    }
    if (event.frame_status != fuchsia::camera::common::FrameStatus::OK) {
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
        image_ids_[event.buffer_id], now_ns, std::move(acquire_fences), std::move(release_fences),
        [this](fuchsia::images::PresentationInfo presentation_info) { SleepIfChaos(); });

    auto waiter = std::make_unique<async::Wait>(
        release_fence.get(), ZX_EVENT_SIGNALED, 0,
        [this, event](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                      const zx_packet_signal_t* signal) {
          if (status != ZX_OK) {
            FXL_PLOG(ERROR, status) << "Wait failed";
            loop_->Quit();
            return;
          }
          SleepIfChaos();
          stream_->ReleaseFrame(event.buffer_id);
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
  fuchsia::camera::common::StreamPtr stream_;
  scenic::ShapeNode node_;
  fuchsia::images::ImagePipePtr image_pipe_;
  std::map<uint32_t, uint32_t> image_ids_;
  float shape_width_;
  std::queue<std::pair<std::unique_ptr<async::Wait>, zx::event>> waiters_;
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
