// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_RENDERER_IMPL_H_
#define APPS_MOZART_SRC_COMPOSITOR_RENDERER_IMPL_H_

#include <functional>

#include "apps/mozart/services/composition/renderers.fidl.h"
#include "apps/mozart/src/compositor/compositor_engine.h"
#include "apps/mozart/src/compositor/renderer_state.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"

namespace compositor {

// Renderer interface implementation.
// This object is owned by its associated RendererState.
class RendererImpl : public mozart::Renderer,
                     public mozart::FrameScheduler,
                     public mozart::HitTester {
 public:
  RendererImpl(CompositorEngine* engine,
               RendererState* state,
               fidl::InterfaceRequest<mozart::Renderer> renderer_request);
  ~RendererImpl() override;

  void set_connection_error_handler(const ftl::Closure& handler) {
    renderer_binding_.set_connection_error_handler(handler);
  }

 private:
  // |Renderer|:
  void GetDisplayInfo(const GetDisplayInfoCallback& callback) override;
  void SetRootScene(mozart::SceneTokenPtr scene_token,
                    uint32_t scene_version,
                    mozart::RectPtr viewport) override;
  void ClearRootScene() override;
  void GetScheduler(fidl::InterfaceRequest<mozart::FrameScheduler>
                        scheduler_request) override;
  void GetHitTester(
      fidl::InterfaceRequest<mozart::HitTester> hit_tester_request) override;

  // |FrameScheduler|:
  void ScheduleFrame(const ScheduleFrameCallback& callback) override;

  // |HitTester|:
  void HitTest(mozart::PointFPtr point,
               const HitTestCallback& callback) override;

  CompositorEngine* const engine_;
  RendererState* const state_;
  fidl::Binding<mozart::Renderer> renderer_binding_;
  fidl::BindingSet<mozart::FrameScheduler> scheduler_bindings_;
  fidl::BindingSet<mozart::HitTester> hit_tester_bindings;

  FTL_DISALLOW_COPY_AND_ASSIGN(RendererImpl);
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_RENDERER_IMPL_H_
