// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_EXAMPLES_TILE_TILE_VIEW_H_
#define APPS_MOZART_EXAMPLES_TILE_TILE_VIEW_H_

#include <map>
#include <memory>

#include "apps/modular/services/application/application_launcher.fidl.h"
#include "apps/mozart/examples/tile/tile_params.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "lib/ftl/macros.h"

namespace examples {

class TileView : public mozart::BaseView {
 public:
  TileView(mozart::ViewManagerPtr view_manager,
           fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
           modular::ApplicationLauncher* application_launcher,
           const TileParams& tile_params);

  ~TileView() override;

 private:
  struct ViewData {
    explicit ViewData(const std::string& url,
                      uint32_t key,
                      modular::ApplicationControllerPtr controller);
    ~ViewData();

    const std::string url;
    const uint32_t key;
    modular::ApplicationControllerPtr controller;

    mozart::RectF layout_bounds;
    mozart::ViewPropertiesPtr view_properties;
    mozart::ViewInfoPtr view_info;
    uint32_t scene_version = 1u;
  };

  // |BaseView|:
  void OnLayout() override;
  void OnDraw() override;
  void OnChildAttached(uint32_t child_key,
                       mozart::ViewInfoPtr child_view_info) override;
  void OnChildUnavailable(uint32_t child_key) override;

  void ConnectViews();
  void UpdateScene();

  modular::ApplicationLauncher* application_launcher_;
  TileParams params_;
  std::map<uint32_t, std::unique_ptr<ViewData>> views_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TileView);
};

}  // namespace examples

#endif  // APPS_MOZART_EXAMPLES_TILE_TILE_VIEW_H_
