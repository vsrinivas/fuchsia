// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/backend/framebuffer_output.h"

#include <algorithm>
#include <utility>

#include <mx/process.h>
#include <mx/vmo.h>

#include "apps/mozart/glue/base/trace_event.h"
#include "apps/mozart/src/compositor/render/render_frame.h"
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
  Rasterizer(FramebufferOutput* output,
             mojo::InterfaceHandle<mojo::Framebuffer> framebuffer,
             mojo::FramebufferInfoPtr framebuffer_info);
  ~Rasterizer();

  void DrawFrame(ftl::RefPtr<RenderFrame> frame,
                 uint32_t frame_number,
                 ftl::TimePoint submit_time);

 private:
  FramebufferOutput* output_;

  mojo::FramebufferPtr framebuffer_;
  mojo::FramebufferInfoPtr framebuffer_info_;
  uintptr_t framebuffer_data_ = 0u;
  sk_sp<SkSurface> framebuffer_surface_;

  bool awaiting_flush_ = false;
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

void FramebufferOutput::Initialize(
    mojo::InterfaceHandle<mojo::Framebuffer> framebuffer,
    mojo::FramebufferInfoPtr framebuffer_info,
    ftl::Closure error_callback) {
  FTL_DCHECK(!rasterizer_);

  error_callback_ = error_callback;

  ftl::ManualResetWaitableEvent wait;
  rasterizer_thread_ = mtl::CreateThread(&rasterizer_task_runner_);
  rasterizer_task_runner_->PostTask(ftl::MakeCopyable([
    this, &wait, fb = std::move(framebuffer), fbi = std::move(framebuffer_info)
  ]() mutable {
    rasterizer_ =
        std::make_unique<Rasterizer>(this, std::move(fb), std::move(fbi));
    wait.Signal();
  }));
  wait.Wait();
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

FramebufferOutput::Rasterizer::Rasterizer(
    FramebufferOutput* output,
    mojo::InterfaceHandle<mojo::Framebuffer> framebuffer,
    mojo::FramebufferInfoPtr framebuffer_info)
    : output_(output),
      framebuffer_(mojo::FramebufferPtr::Create(std::move(framebuffer))),
      framebuffer_info_(std::move(framebuffer_info)) {
  FTL_DCHECK(output_);
  FTL_DCHECK(framebuffer_);
  FTL_DCHECK(framebuffer_info_);

  TRACE_EVENT0("gfx", "InitializeRasterizer");

  const uint32_t width = framebuffer_info_->size->width;
  const uint32_t height = framebuffer_info_->size->height;
  const size_t row_bytes = framebuffer_info_->row_bytes;
  const size_t size = row_bytes * height;
  mx_status_t status = mx::process::self().map_vm(
      mx::vmo(framebuffer_info_->vmo.release().value()), 0, size,
      &framebuffer_data_, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
  if (status < 0) {
    FTL_LOG(ERROR) << "Cannot map framebuffer: status=" << status;
    output_->PostErrorCallback();
    return;
  }

  SkColorType sk_color_type;
  SkAlphaType sk_alpha_type;
  sk_sp<SkColorSpace> sk_color_space;
  switch (framebuffer_info_->format) {
    case mojo::FramebufferFormat::RGB_565:
      sk_color_type = kRGB_565_SkColorType;
      sk_alpha_type = kOpaque_SkAlphaType;
      break;
    case mojo::FramebufferFormat::ARGB_8888:   // little-endian packed 32-bit
      sk_color_type = kBGRA_8888_SkColorType;  // ARGB word has BGRA byte order
      sk_alpha_type = kPremul_SkAlphaType;
      sk_color_space = SkColorSpace::NewNamed(SkColorSpace::kSRGB_Named);
      break;
    case mojo::FramebufferFormat::RGB_x888:    // little-endian packed 32-bit
      sk_color_type = kBGRA_8888_SkColorType;  // xRGB word has BGRx byte order
      sk_alpha_type = kOpaque_SkAlphaType;
      sk_color_space = SkColorSpace::NewNamed(SkColorSpace::kSRGB_Named);
      break;
    default:
      FTL_LOG(ERROR) << "Unknown color type: " << framebuffer_info_->format;
      output_->PostErrorCallback();
      return;
  }

  FTL_LOG(INFO) << "Initializing framebuffer: "
                << "width=" << width << ", height=" << height
                << ", row_bytes=" << row_bytes
                << ", format=" << framebuffer_info_->format
                << ", sk_color_type=" << sk_color_type
                << ", sk_alpha_type=" << sk_alpha_type;

  framebuffer_surface_ = SkSurface::MakeRasterDirect(
      SkImageInfo::Make(width, height, sk_color_type, sk_alpha_type,
                        sk_color_space),
      reinterpret_cast<void*>(framebuffer_data_), row_bytes);
  if (!framebuffer_surface_) {
    FTL_LOG(ERROR) << "Failed to initialize framebuffer";
    output_->PostErrorCallback();
    return;
  }
}

FramebufferOutput::Rasterizer::~Rasterizer() {
  if (framebuffer_data_) {
    mx::process::self().unmap_vm(
        framebuffer_data_,
        framebuffer_info_->row_bytes * framebuffer_info_->size->height);
  }
}

void FramebufferOutput::Rasterizer::DrawFrame(ftl::RefPtr<RenderFrame> frame,
                                              uint32_t frame_number,
                                              ftl::TimePoint submit_time) {
  TRACE_EVENT_ASYNC_BEGIN0("gfx", "DrawFrame", frame_number);
  FTL_DCHECK(frame);
  FTL_DCHECK(!awaiting_flush_);

  ftl::TimePoint draw_time = ftl::TimePoint::Now();

  SkCanvas* canvas = framebuffer_surface_->getCanvas();
  frame->Draw(canvas);
  canvas->flush();

  awaiting_flush_ = true;
  framebuffer_->Flush([this, frame_number, submit_time, draw_time]() {
    FTL_DCHECK(awaiting_flush_);
    awaiting_flush_ = false;

    ftl::TimePoint finish_time = ftl::TimePoint::Now();
    output_->PostFrameFinishedFromRasterizer(frame_number, submit_time,
                                             draw_time, finish_time);
    TRACE_EVENT_ASYNC_END0("gfx", "DrawFrame", frame_number);
  });
}

}  // namespace compositor
