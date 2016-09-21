// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_GFX_COMPOSITOR_BACKEND_GPU_RASTERIZER_H_
#define SERVICES_GFX_COMPOSITOR_BACKEND_GPU_RASTERIZER_H_

#include <memory>

#include "apps/compositor/glue/gl/gl_context.h"
#include "apps/compositor/glue/skia/ganesh_context.h"
#include "apps/compositor/glue/skia/ganesh_framebuffer_surface.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/tasks/one_shot_timer.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/services/gpu/interfaces/context_provider.mojom.h"

namespace compositor {

class RenderFrame;

// Ganesh-based rasterizer.
// Maintains a GL context and draws frames on demand.
//
// This object runs on a separate thread from the rest of the compositor.
// It is not threadsafe; all calls into this object, including its creation,
// must run on the rasterizer thread.
class GpuRasterizer : public mojo::ViewportParameterListener,
                      public mojo::GLContext::Observer {
 public:
  // Callbacks from the rasterizer.
  // These calls always run on the rasterizer thread.
  class Callbacks {
   public:
    virtual ~Callbacks() {}

    // Called when the rasterizer is ready to start drawing.
    // May be called repeatedly with new parameters.
    virtual void OnRasterizerReady(int64_t vsync_timebase,
                                   int64_t vsync_interval) = 0;

    // Called when the rasterizer can't draw anymore.
    virtual void OnRasterizerSuspended() = 0;

    // Called when the rasterizer finished drawing a frame.
    // |presented| is true if the frame was actually presented, false if
    // the frame was discarded.
    virtual void OnRasterizerFinishedDraw(bool presented) = 0;

    // Called when an unrecoverable error occurs and the rasterizer needs
    // to be shut down soon.
    virtual void OnRasterizerError() = 0;
  };

  GpuRasterizer(mojo::ContextProviderPtr context_provider,
                Callbacks* callbacks);
  ~GpuRasterizer() override;

  // Draws the specified frame.
  // Each frame will be acknowledged by a called to |OnRasterizerFinishedDraw|
  // in the order submitted.  The rasterizer must be in a ready state.
  void DrawFrame(const ftl::RefPtr<RenderFrame>& frame);

 private:
  // |ViewportParameterListener|:
  void OnVSyncParametersUpdated(int64_t timebase, int64_t interval) override;

  // |GLContext::Observer|:
  void OnContextLost() override;

  void CreateContext();
  void InitContext(mojo::InterfaceHandle<mojo::CommandBuffer> command_buffer);
  void AbandonContext();
  void DestroyContext();
  void RecreateContextAfterLoss();
  void OnContextProviderConnectionError();
  void OnViewportParameterTimeout();
  void ApplyViewportParameters();

  void DrawFinished(bool presented);
  static void OnMGLEchoReply(void* context);

  mojo::ContextProviderPtr context_provider_;
  Callbacks* callbacks_;

  ftl::RefPtr<mojo::GLContext> gl_context_;
  ftl::RefPtr<mojo::skia::GaneshContext> ganesh_context_;
  std::unique_ptr<mojo::skia::GaneshFramebufferSurface> ganesh_surface_;

  mojo::Binding<ViewportParameterListener> viewport_parameter_listener_binding_;
  ftl::OneShotTimer viewport_parameter_timeout_;
  bool have_viewport_parameters_ = false;
  int64_t vsync_timebase_ = 0u;
  int64_t vsync_interval_ = 0u;

  bool ready_ = false;
  uint32_t total_frames_ = 0u;
  uint32_t frames_in_progress_ = 0u;

  ftl::WeakPtrFactory<GpuRasterizer> weak_ptr_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GpuRasterizer);
};

}  // namespace compositor

#endif  // SERVICES_GFX_COMPOSITOR_BACKEND_GPU_RASTERIZER_H_
