// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/environment/scoped_chromium_init.h"
#include "mojo/public/c/system/main.h"
#include "mojo/public/cpp/application/run_application.h"
#include "services/ui/launcher/launcher_app.h"

MojoResult MojoMain(MojoHandle application_request) {
  mojo::ScopedChromiumInit init;
  launcher::LauncherApp launcher_app;
  return mojo::RunApplication(application_request, &launcher_app);
}
