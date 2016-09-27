// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_UI_TILE_TILE_APP_H_
#define EXAMPLES_UI_TILE_TILE_APP_H_

#include "apps/mozart/lib/view_framework/view_provider_app.h"
#include "lib/ftl/macros.h"

namespace examples {

struct TileParams;

class TileApp : public mojo::ui::ViewProviderApp {
 public:
  TileApp();
  ~TileApp() override;

  void CreateView(
      const std::string& connection_url,
      mojo::InterfaceRequest<mojo::ui::ViewOwner> view_owner_request,
      mojo::InterfaceRequest<mojo::ServiceProvider> services) override;

 private:
  bool ParseParams(const std::string& connection_url, TileParams* params);

  FTL_DISALLOW_COPY_AND_ASSIGN(TileApp);
};

}  // namespace examples

#endif  // EXAMPLES_UI_TILE_TILE_APP_H_
