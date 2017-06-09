// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_COMPOSITOR_APP_H_
#define APPS_MOZART_SRC_COMPOSITOR_COMPOSITOR_APP_H_

#include <memory>

#include "application/lib/app/application_context.h"
#include "apps/mozart/services/composition/compositor.fidl.h"
#include "apps/mozart/src/compositor/compositor_engine.h"
#include "apps/mozart/src/compositor/config.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"

namespace compositor {

class CompositorImpl;

// Compositor application entry point.
class CompositorApp {
 public:
  CompositorApp();
  ~CompositorApp();

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;
  std::unique_ptr<CompositorEngine> engine_;
  fidl::BindingSet<mozart::Compositor, std::unique_ptr<CompositorImpl>>
      compositor_bindings_;
  Config config_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CompositorApp);
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_COMPOSITOR_APP_H_
