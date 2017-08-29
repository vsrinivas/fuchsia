// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "application/lib/app/application_context.h"
#include "apps/mozart/examples/shadertoy/service/services/shadertoy.fidl.h"
#include "apps/mozart/examples/shadertoy/service/services/shadertoy_factory.fidl.h"
#include "apps/mozart/lib/scenic/client/resources.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "lib/ftl/macros.h"

namespace shadertoy_client {

class View : public mozart::BaseView {
 public:
  View(app::ApplicationContext* application_context,
       mozart::ViewManagerPtr view_manager,
       fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request);

  ~View() override;

 private:
  // |BaseView|.
  void OnSceneInvalidated(
      scenic::PresentationInfoPtr presentation_info) override;

  app::ApplicationContext* const application_context_;
  mtl::MessageLoop* loop_;

  mozart::example::ShadertoyFactoryPtr shadertoy_factory_;
  mozart::example::ShadertoyPtr shadertoy_;

  std::vector<scenic_lib::ShapeNode> nodes_;

  const mx_time_t start_time_;

  FTL_DISALLOW_COPY_AND_ASSIGN(View);
};

}  // namespace shadertoy_client
