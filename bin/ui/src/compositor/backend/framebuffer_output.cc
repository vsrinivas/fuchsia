// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/backend/framebuffer_output.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "apps/mozart/glue/base/trace_event.h"
#include "apps/mozart/src/compositor/backend/framebuffer.h"
#include "apps/mozart/src/compositor/render/render_frame.h"
#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/create_thread.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace compositor {
namespace {

// Delay between frames.
// TODO(jeffbrown): Don't hardcode this.
constexpr ftl::TimeDelta kHardwareRefreshInterval =
    ftl::TimeDelta::FromMicroseconds(16667);

// Amount of time it takes between flushing a frame and pixels lighting up.
// TODO(jeffbrown): Tune this for A/V sync.
constexpr ftl::TimeDelta kHardwareDisplayLatency =
    ftl::TimeDelta::FromMicroseconds(1000);

}  // namespace

class FramebufferOutput::Rasterizer {
 public:
  explicit Rasterizer(FramebufferOutput* output);
  ~Rasterizer();

  mozart::DisplayInfoPtr GetDisplayInfo();

  void DrawFrame(ftl::RefPtr<RenderFrame> frame,
                 uint32_t frame_number,
                 ftl::TimePoint submit_time);

 private:
  bool Initialize();

  FramebufferOutput* output_;

  std::unique_ptr<Framebuffer> framebuffer_;
  sk_sp<SkSurface> framebuffer_surface_;
};

FramebufferOutput::FramebufferOutput()
    : compositor_task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()) {}

FramebufferOutput::~FramebufferOutput() {
  if (rasterizer_) {
    rasterizer_task_runner_->PostTask([this] {
      rasterizer_.reset();
      mtl::MessageLoop::GetCurrent()->QuitNow();
    });
    rasterizer_thread_.join();
  }
}

void FramebufferOutput::Initialize(ftl::Closure error_callback) {
  FTL_DCHECK(!rasterizer_);

  error_callback_ = error_callback;

  ftl::ManualResetWaitableEvent wait;
  rasterizer_thread_ = mtl::CreateThread(&rasterizer_task_runner_);
  rasterizer_task_runner_->PostTask([this, &wait] {
    rasterizer_ = std::make_unique<Rasterizer>(this);
    wait.Signal();
  });
  wait.Wait();
}

void FramebufferOutput::GetDisplayInfo(DisplayCallback callback) {
  FTL_DCHECK(rasterizer_);

  rasterizer_task_runner_->PostTask(
      [this, callback] { callback(rasterizer_->GetDisplayInfo()); });
}

void FramebufferOutput::ScheduleFrame(FrameCallback callback) {
  FTL_DCHECK(callback);
  FTL_DCHECK(!scheduled_frame_callback_);
  FTL_DCHECK(rasterizer_);

  scheduled_frame_callback_ = callback;

  if (!frame_in_progress_)
    RunScheduledFrameCallback();
}

void FramebufferOutput::SubmitFrame(ftl::RefPtr<RenderFrame> frame) {
  FTL_DCHECK(frame);
  FTL_DCHECK(rasterizer_);
  frame_number_++;
  TRACE_EVENT_ASYNC_BEGIN0("gfx", "SubmitFrame", frame_number_);

  if (frame_in_progress_) {
    if (next_frame_) {
      FTL_DLOG(WARNING) << "Discarded a frame to catch up";
      TRACE_EVENT_ASYNC_END0("gfx", "SubmitFrame", frame_number_ - 1);
    }
    next_frame_ = frame;
    return;
  }

  frame_in_progress_ = true;
  PostFrameToRasterizer(std::move(frame));
}

void FramebufferOutput::PostErrorCallback() {
  compositor_task_runner_->PostTask(error_callback_);
}

void FramebufferOutput::PostFrameToRasterizer(ftl::RefPtr<RenderFrame> frame) {
  FTL_DCHECK(frame_in_progress_);
  rasterizer_task_runner_->PostTask(ftl::MakeCopyable([
    this, frame = std::move(frame), frame_number = frame_number_,
    submit_time = ftl::TimePoint::Now()
  ]() mutable {
    rasterizer_->DrawFrame(std::move(frame), frame_number, submit_time);
  }));
}

void FramebufferOutput::PostFrameFinishedFromRasterizer(
    uint32_t frame_number,
    ftl::TimePoint submit_time,
    ftl::TimePoint draw_time,
    ftl::TimePoint finish_time) {
  // TODO(jeffbrown): Tally these statistics.
  compositor_task_runner_->PostTask(
      [this, frame_number, submit_time, finish_time] {
        FTL_DCHECK(frame_in_progress_);

        last_presentation_time_ = finish_time + kHardwareDisplayLatency;

        // TODO(jeffbrown): Filter this feedback loop to avoid large swings.
        presentation_latency_ = last_presentation_time_ - submit_time;
        FTL_LOG(INFO) << "presentation_latency_="
                      << presentation_latency_.ToMicroseconds();

        TRACE_EVENT_ASYNC_END0("gfx", "SubmitFrame", frame_number);

        if (next_frame_) {
          PostFrameToRasterizer(std::move(next_frame_));
        } else {
          frame_in_progress_ = false;
          if (scheduled_frame_callback_)
            RunScheduledFrameCallback();
        }
      });
}

void FramebufferOutput::RunScheduledFrameCallback() {
  FTL_DCHECK(scheduled_frame_callback_);
  FTL_DCHECK(!frame_in_progress_);

  FrameTiming timing;
  timing.presentation_time =
      std::max(last_presentation_time_ + kHardwareRefreshInterval,
               ftl::TimePoint::Now());
  timing.presentation_interval = kHardwareRefreshInterval;
  timing.presentation_latency = presentation_latency_;

  FrameCallback callback;
  scheduled_frame_callback_.swap(callback);
  callback(timing);
}

FramebufferOutput::Rasterizer::Rasterizer(FramebufferOutput* output)
    : output_(output) {
  FTL_DCHECK(output_);

  if (!Initialize())
    output_->PostErrorCallback();
}

bool FramebufferOutput::Rasterizer::Initialize() {
  TRACE_EVENT0("gfx", "InitializeRasterizer");

  framebuffer_ = Framebuffer::Open();
  if (!framebuffer_) {
    FTL_LOG(ERROR) << "Failed to open framebuffer";
    return false;
  }

  SkColorType sk_color_type;
  switch (framebuffer_->info().format) {
    case MX_PIXEL_FORMAT_ARGB_8888:
    case MX_PIXEL_FORMAT_RGB_x888:
      sk_color_type = kBGRA_8888_SkColorType;
      break;
    case MX_PIXEL_FORMAT_RGB_565:
      sk_color_type = kRGB_565_SkColorType;
      break;
    default:
      FTL_LOG(ERROR) << "Framebuffer has unsupported pixel format: "
                     << framebuffer_->info().format;
      return false;
  }

  framebuffer_surface_ = mozart::MakeSkSurfaceFromVMO(
      framebuffer_->vmo(),
      SkImageInfo::Make(framebuffer_->info().width, framebuffer_->info().height,
                        sk_color_type, kOpaque_SkAlphaType),
      framebuffer_->info().stride * framebuffer_->info().pixelsize);
  if (!framebuffer_surface_) {
    FTL_LOG(ERROR) << "Failed to map framebuffer surface";
    return false;
  }

  return true;
}

FramebufferOutput::Rasterizer::~Rasterizer() {}

mozart::DisplayInfoPtr FramebufferOutput::Rasterizer::GetDisplayInfo() {
  auto result = mozart::DisplayInfo::New();
  result->size = mojo::Size::New();
  result->size->width = framebuffer_->info().width;
  result->size->height = framebuffer_->info().height;
  result->device_pixel_ratio = 1.f;  // TODO: don't hardcode this
  return result;
}

void FramebufferOutput::Rasterizer::DrawFrame(ftl::RefPtr<RenderFrame> frame,
                                              uint32_t frame_number,
                                              ftl::TimePoint submit_time) {
  TRACE_EVENT_ASYNC_BEGIN0("gfx", "DrawFrame", frame_number);
  FTL_DCHECK(frame);

  ftl::TimePoint draw_time = ftl::TimePoint::Now();

  SkCanvas* canvas = framebuffer_surface_->getCanvas();
  frame->Draw(canvas);
  canvas->flush();

  framebuffer_->Flush();

  ftl::TimePoint finish_time = ftl::TimePoint::Now();
  output_->PostFrameFinishedFromRasterizer(frame_number, submit_time, draw_time,
                                           finish_time);
  TRACE_EVENT_ASYNC_END0("gfx", "DrawFrame", frame_number);
}

}  // namespace compositor
