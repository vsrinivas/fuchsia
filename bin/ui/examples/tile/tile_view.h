// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_UI_TILE_TILE_VIEW_H_
#define EXAMPLES_UI_TILE_TILE_VIEW_H_

#include <map>
#include <memory>
#include <vector>

#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/views/interfaces/view_provider.mojom.h"
#include "lib/ftl/macros.h"

namespace examples {

struct TileParams {
  enum class VersionMode {
    kAny,    // specify |kSceneVersionNone|
    kExact,  // specify exact version
  };
  enum class CombinatorMode {
    kMerge,          // use merge combinator
    kPrune,          // use prune combinator
    kFallbackFlash,  // use fallback combinator with red flash
    kFallbackDim,    // use fallback combinator with old content dimmed
  };
  enum class OrientationMode {
    kHorizontal,
    kVertical,
  };

  TileParams();
  ~TileParams();

  VersionMode version_mode = VersionMode::kAny;
  CombinatorMode combinator_mode = CombinatorMode::kPrune;
  OrientationMode orientation_mode = OrientationMode::kHorizontal;

  std::vector<std::string> view_urls;
};

class TileView : public mozart::BaseView {
 public:
  TileView(mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
           mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request,
           const TileParams& tile_params);

  ~TileView() override;

 private:
  struct ViewData {
    explicit ViewData(const std::string& url, uint32_t key);
    ~ViewData();

    const std::string url;
    const uint32_t key;

    mojo::RectF layout_bounds;
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

  TileParams params_;
  std::map<uint32_t, std::unique_ptr<ViewData>> views_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TileView);
};

}  // namespace examples

#endif  // EXAMPLES_UI_TILE_TILE_VIEW_H_
