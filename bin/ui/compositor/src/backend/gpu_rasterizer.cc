// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/compositor/src/backend/gpu_rasterizer.h"

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif

#include <GLES2/gl2.h>
#include <GLES2/gl2extmojo.h>
#include <MGL/mgl.h>
#include <MGL/mgl_echo.h>
#include <MGL/mgl_onscreen.h>

#include "apps/compositor/glue/base/logging.h"
#include "apps/compositor/glue/base/trace_event.h"
#include "apps/compositor/src/render/render_frame.h"
#include "lib/ftl/functional/make_runnable.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace compositor {
namespace {
// Timeout for receiving initial viewport parameters from the GPU service.
constexpr int64_t kViewportParameterTimeoutMs = 1000;

// Default vsync interval when the GPU service failed to provide viewport
// parameters promptly.
constexpr int64_t kDefaultVsyncIntervalUs = 100000;  // deliberately sluggish
}

GpuRasterizer::GpuRasterizer(mojo::ContextProviderPtr context_provider,
                             Callbacks* callbacks)
    : context_provider_(context_provider.Pass()),
      callbacks_(callbacks),
      viewport_parameter_listener_binding_(this),
      weak_ptr_factory_(this) {
  FTL_DCHECK(context_provider_);
  FTL_DCHECK(callbacks_);

  context_provider_.set_connection_error_handler(
      [this] { OnContextProviderConnectionError(); });
  CreateContext();
}

GpuRasterizer::~GpuRasterizer() {
  DestroyContext();
}

void GpuRasterizer::CreateContext() {
  FTL_DCHECK(!gl_context_);

  have_viewport_parameters_ = false;

  mojo::ViewportParameterListenerPtr viewport_parameter_listener;
  viewport_parameter_listener_binding_.Bind(
      GetProxy(&viewport_parameter_listener));
  context_provider_->Create(
      std::move(viewport_parameter_listener),
      [this](mojo::InterfaceHandle<mojo::CommandBuffer> command_buffer) {
        InitContext(std::move(command_buffer));
      });
}

void GpuRasterizer::InitContext(
    mojo::InterfaceHandle<mojo::CommandBuffer> command_buffer) {
  FTL_DCHECK(!gl_context_);
  FTL_DCHECK(!ganesh_context_);
  FTL_DCHECK(!ganesh_surface_);

  if (!command_buffer) {
    FTL_LOG(ERROR) << "Could not create GL context.";
    callbacks_->OnRasterizerError();
    return;
  }

  gl_context_ = mojo::GLContext::CreateFromCommandBuffer(
      mojo::CommandBufferPtr::Create(std::move(command_buffer)));
  FTL_DCHECK(!gl_context_->is_lost());
  gl_context_->AddObserver(this);
  ganesh_context_ = ftl::MakeRefCounted<mojo::skia::GaneshContext>(gl_context_);

  if (have_viewport_parameters_) {
    ApplyViewportParameters();
  } else {
    viewport_parameter_timeout_.Start(
        mtl::MessageLoop::GetCurrent()->task_runner(),
        [this] { OnViewportParameterTimeout(); },
        ftl::TimeDelta::FromMilliseconds(kViewportParameterTimeoutMs));
  }
}

void GpuRasterizer::AbandonContext() {
  if (viewport_parameter_listener_binding_.is_bound()) {
    viewport_parameter_timeout_.Stop();
    viewport_parameter_listener_binding_.Close();
  }

  if (ready_) {
    while (frames_in_progress_)
      DrawFinished(false /*presented*/);
    ready_ = false;
    callbacks_->OnRasterizerSuspended();
  }
}

void GpuRasterizer::DestroyContext() {
  AbandonContext();

  if (gl_context_) {
    ganesh_context_ = nullptr;
    gl_context_ = nullptr;

    // Do this after releasing the GL context so that we will already have
    // told the Ganesh context to abandon its context.
    ganesh_surface_.reset();
  }
}

void GpuRasterizer::OnContextProviderConnectionError() {
  FTL_LOG(ERROR) << "Context provider connection lost.";

  callbacks_->OnRasterizerError();
}

void GpuRasterizer::OnContextLost() {
  FTL_LOG(WARNING) << "GL context lost!";

  AbandonContext();
  mtl::MessageLoop::GetCurrent()->task_runner()->PostTask(
      [weak_ptr = weak_ptr_factory_.GetWeakPtr()] {
        if (weak_ptr)
          weak_ptr->RecreateContextAfterLoss();
      });
}

void GpuRasterizer::RecreateContextAfterLoss() {
  FTL_LOG(WARNING) << "Recreating GL context.";

  DestroyContext();
  CreateContext();
}

void GpuRasterizer::OnViewportParameterTimeout() {
  FTL_DCHECK(!have_viewport_parameters_);

  FTL_LOG(WARNING) << "Viewport parameter listener timeout after "
                   << kViewportParameterTimeoutMs << " ms: assuming "
                   << kDefaultVsyncIntervalUs
                   << " us vsync interval, rendering will be janky!";

  OnVSyncParametersUpdated(0, kDefaultVsyncIntervalUs);
}

void GpuRasterizer::OnVSyncParametersUpdated(int64_t timebase,
                                             int64_t interval) {
  DVLOG(1) << "Vsync parameters: timebase=" << timebase
           << ", interval=" << interval;

  if (!have_viewport_parameters_) {
    viewport_parameter_timeout_.Stop();
    have_viewport_parameters_ = true;
  }
  vsync_timebase_ = timebase;
  vsync_interval_ = interval;
  ApplyViewportParameters();
}

void GpuRasterizer::ApplyViewportParameters() {
  FTL_DCHECK(have_viewport_parameters_);

  if (gl_context_ && !gl_context_->is_lost()) {
    ready_ = true;
    callbacks_->OnRasterizerReady(vsync_timebase_, vsync_interval_);
  }
}

void GpuRasterizer::DrawFrame(const ftl::RefPtr<RenderFrame>& frame) {
  FTL_DCHECK(frame);
  FTL_DCHECK(ready_);
  FTL_DCHECK(gl_context_);
  FTL_DCHECK(!gl_context_->is_lost());
  FTL_DCHECK(ganesh_context_);

  uint32_t frame_number = total_frames_++;
  frames_in_progress_++;
  TRACE_EVENT1("gfx", "GpuRasterizer::DrawFrame", "num", frame_number);

  mojo::GLContext::Scope gl_scope(gl_context_);

  // Update the viewport.
  const SkIRect& viewport = frame->viewport();
  bool stale_surface = false;
  if (!ganesh_surface_ ||
      ganesh_surface_->surface()->width() != viewport.width() ||
      ganesh_surface_->surface()->height() != viewport.height()) {
    glResizeCHROMIUM(viewport.width(), viewport.height(), 1.0f);
    glViewport(viewport.x(), viewport.y(), viewport.width(), viewport.height());
    stale_surface = true;
  }

  // Draw the frame content.
  {
    mojo::skia::GaneshContext::Scope ganesh_scope(ganesh_context_);

    if (stale_surface) {
      ganesh_surface_.reset(
          new mojo::skia::GaneshFramebufferSurface(ganesh_scope));
    }

    frame->Draw(ganesh_surface_->canvas());
  }

  // Swap buffers.
  {
    TRACE_EVENT0("gfx", "MGLSwapBuffers");
    MGLSwapBuffers();
  }

  // Listen for completion.
  TRACE_EVENT_ASYNC_BEGIN0("gfx", "MGLEcho", frame_number);
  MGLEcho(&GpuRasterizer::OnMGLEchoReply, this);
}

void GpuRasterizer::DrawFinished(bool presented) {
  FTL_DCHECK(frames_in_progress_);

  uint32_t frame_number = total_frames_ - frames_in_progress_;
  frames_in_progress_--;
  TRACE_EVENT2("gfx", "GpuRasterizer::DrawFinished", "num", frame_number,
               "presented", presented);
  TRACE_EVENT_ASYNC_END0("gfx", "MGLEcho", frame_number);

  callbacks_->OnRasterizerFinishedDraw(presented);
}

void GpuRasterizer::OnMGLEchoReply(void* context) {
  auto rasterizer = static_cast<GpuRasterizer*>(context);
  if (rasterizer->ready_)
    rasterizer->DrawFinished(true /*presented*/);
}

}  // namespace compositor
