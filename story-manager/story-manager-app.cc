// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/story-manager/story-manager-app.h"

#include <stdio.h>

#include "lib/ftl/logging.h"

namespace story_manager {

StoryManagerApp::StoryManagerApp() {}

StoryManagerApp::~StoryManagerApp() {}

void StoryManagerApp::OnInitialize() {
  // HACH(alhaad): This is a temporary way to bootstrap the system. According
  // to design, a 'device-shell' will provide this information over a mojo
  // interface.
  if (args().size() != 3) {
    FTL_DLOG(INFO) << "mojo:story-manager expects 2 additional arguments.\n"
                   << "Usage: mojo:story-manager [user] [recipe]";
    return;
  }

  FTL_DLOG(INFO) << "Starting story-manager for: " << args()[1].c_str();
  FTL_DLOG(INFO) << "Attempting to start story-runner with: " << args()[2];
  FTL_DLOG(ERROR) << "Interface to story-runner unimplemented.";
}

}  // namespace story_manager
