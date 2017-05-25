// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/services/composition/compositor.fidl.h"
#include "apps/mozart/src/view_manager/view_registry.h"

namespace view_manager {

class ViewRegistry1 : public ViewRegistry {
 public:
  explicit ViewRegistry1(app::ApplicationContext* application_context,
                         mozart::CompositorPtr compositor);

 private:
  // Implement ViewRegistry pure virtual methods.
  void CreateScene(ViewState* view_state,
                   fidl::InterfaceRequest<mozart::Scene> scene) override;
  void AttachResolvedViewAndNotify(ViewStub* view_stub,
                                   ViewState* view_state) override;
  std::unique_ptr<ViewImpl> CreateViewImpl() override;

  mozart::CompositorPtr compositor_;
};

}  // namespace view_manager
