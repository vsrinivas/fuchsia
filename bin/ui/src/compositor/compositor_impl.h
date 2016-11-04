// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_COMPOSITOR_IMPL_H_
#define APPS_MOZART_SRC_COMPOSITOR_COMPOSITOR_IMPL_H_

#include "apps/mozart/services/composition/interfaces/compositor.mojom.h"
#include "apps/mozart/src/compositor/compositor_engine.h"
#include "lib/ftl/macros.h"

namespace compositor {

// Compositor interface implementation.
class CompositorImpl : public mozart::Compositor {
 public:
  explicit CompositorImpl(CompositorEngine* engine);
  ~CompositorImpl() override;

 private:
  // |Compositor|:
  void CreateScene(mojo::InterfaceRequest<mozart::Scene> scene_request,
                   const mojo::String& label,
                   const CreateSceneCallback& callback) override;
  void CreateRenderer(mojo::InterfaceRequest<mozart::Renderer> renderer_request,
                      const mojo::String& label) override;

  CompositorEngine* engine_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CompositorImpl);
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_COMPOSITOR_IMPL_H_
