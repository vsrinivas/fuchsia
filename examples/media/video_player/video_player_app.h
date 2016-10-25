// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/lib/view_framework/view_provider_app.h"
#include "lib/ftl/macros.h"

namespace examples {

// Video example app
class VideoPlayerApp : public mozart::ViewProviderApp {
 public:
  VideoPlayerApp();
  ~VideoPlayerApp() override;

  void OnInitialize() override;

  void CreateView(
      const std::string& connection_url,
      mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      mojo::InterfaceRequest<mojo::ServiceProvider> services) override;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(VideoPlayerApp);
};

}  // namespace examples
