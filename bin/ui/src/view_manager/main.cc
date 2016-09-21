// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/environment/scoped_chromium_init.h"
#include "mojo/public/c/system/main.h"
#include "mojo/public/cpp/application/run_application.h"
#include "services/ui/view_manager/view_manager_app.h"

MojoResult MojoMain(MojoHandle application_request) {
  mojo::ScopedChromiumInit init;
  view_manager::ViewManagerApp view_manager_app;
  return mojo::RunApplication(application_request, &view_manager_app);
}
