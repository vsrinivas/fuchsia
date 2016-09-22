// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/backend/framebuffer_output.h"

#include <utility>

#include <magenta/process.h>
#include <magenta/syscalls.h>

#include "apps/mozart/glue/base/trace_event.h"
#include "apps/mozart/src/compositor/render/render_frame.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/skia/include/core/SkCanvas.h"

namespace compositor {

FramebufferOutput::FramebufferOutput(
    mojo::InterfaceHandle<mojo::Framebuffer> framebuffer,
    mojo::FramebufferInfoPtr framebuffer_info,
    const SchedulerCallbacks& scheduler_callbacks,
    const ftl::Closure& error_callback)
    : framebuffer_(mojo::FramebufferPtr::Create(std::move(framebuffer))),
      framebuffer_info_(std::move(framebuffer_info)),
      compositor_task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()),
      vsync_scheduler_(
          ftl::MakeRefCounted<VsyncScheduler>(compositor_task_runner_,
                                              scheduler_callbacks)),
      error_callback_(error_callback) {
  FTL_DCHECK(framebuffer_);
  FTL_DCHECK(framebuffer_info_);

  size_t row_bytes = framebuffer_info_->row_bytes;
  size_t size = row_bytes * framebuffer_info_->size->height;
  mx_status_t status = mx_process_map_vm(
      mx_process_self(), framebuffer_info_->vmo.get().value(), 0, size,
      &framebuffer_data_, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
  if (status < 0) {
    FTL_LOG(ERROR) << "Cannot map framebuffer: status=" << status;
    PostErrorCallback();
    return;
  }

  SkColorType sk_color_type;
  switch (framebuffer_info_->format) {
    case mojo::FramebufferFormat::RGB_565:
      sk_color_type = kRGB_565_SkColorType;
      break;
    case mojo::FramebufferFormat::ARGB_8888:
      sk_color_type = kRGBA_8888_SkColorType;  // different naming convention
      break;
    default:
      FTL_LOG(WARNING) << "Unknown color type: " << framebuffer_info_->format;
      sk_color_type = kRGB_565_SkColorType;
      break;
  }

  framebuffer_surface_ = SkSurface::MakeRasterDirect(
      SkImageInfo::Make(framebuffer_info_->size->width,
                        framebuffer_info_->size->height, sk_color_type,
                        kPremul_SkAlphaType),
      reinterpret_cast<void*>(framebuffer_data_), row_bytes);
  FTL_CHECK(framebuffer_surface_);

  // TODO(jeffbrown): Remove this silliness.
  vsync_scheduler_->Start(0, 16666u, -16666, -2000, 0);
}

FramebufferOutput::~FramebufferOutput() {
  if (framebuffer_data_) {
    mx_process_unmap_vm(
        mx_process_self(), framebuffer_data_,
        framebuffer_info_->row_bytes * framebuffer_info_->size->height);
  }
}

Scheduler* FramebufferOutput::GetScheduler() {
  return vsync_scheduler_.get();
}

void FramebufferOutput::SubmitFrame(const ftl::RefPtr<RenderFrame>& frame) {
  FTL_DCHECK(frame);
  TRACE_EVENT0("gfx", "FramebufferOutput::SubmitFrame");

  SkCanvas* canvas = framebuffer_surface_->getCanvas();
  frame->Draw(canvas);
  canvas->flush();

  framebuffer_->Flush([this]() { /* todo: scheduling feedback */ });
}

void FramebufferOutput::PostErrorCallback() {
  compositor_task_runner_->PostTask(error_callback_);
}

}  // namespace compositor
