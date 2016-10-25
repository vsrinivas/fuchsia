// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/examples/video_player/video_player_app.h"

#include "apps/media/cpp/flog.h"
#include "apps/media/examples/video_player/video_player_params.h"
#include "apps/media/examples/video_player/video_player_view.h"
#include "mojo/public/cpp/application/connect.h"

namespace examples {

VideoPlayerApp::VideoPlayerApp() {}

VideoPlayerApp::~VideoPlayerApp() {
  FLOG_DESTROY();
}

void VideoPlayerApp::OnInitialize() {
  FLOG_INITIALIZE(shell(), "video_player");
}

void VideoPlayerApp::CreateView(
    const std::string& connection_url,
    mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    mojo::InterfaceRequest<mojo::ServiceProvider> services) {
  VideoPlayerParams params(connection_url);

  if (!params.is_valid()) {
    return;
  }

  new VideoPlayerView(mojo::CreateApplicationConnector(shell()),
                      view_owner_request.Pass(), params);
}

}  // namespace examples
