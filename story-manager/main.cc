// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "mojo/public/cpp/application/run_application.h"
#include "apps/modular/story-manager/story-manager-app.h"

MojoResult MojoMain(MojoHandle application_request) {
  story_manager::StoryManagerApp story_manager_app;
  return mojo::RunApplication(application_request, &story_manager_app);
}
