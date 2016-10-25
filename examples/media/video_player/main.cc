// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/examples/video_player/video_player_app.h"
#include "lib/ftl/logging.h"
#include "mojo/public/c/include/mojo/system/main.h"
#include "mojo/public/cpp/application/run_application.h"

MojoResult MojoMain(MojoHandle application_request) {
  FTL_DCHECK(application_request != MOJO_HANDLE_INVALID)
      << "Must be hosted by application_manager";
  examples::VideoPlayerApp video_player_app;
  return mojo::RunApplication(application_request, &video_player_app);
}
