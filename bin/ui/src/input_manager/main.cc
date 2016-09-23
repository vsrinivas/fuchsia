// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/mozart/src/input_manager/input_manager_app.h"
#include "mojo/public/cpp/application/run_application.h"

MojoResult MojoMain(MojoHandle application_request) {
  input_manager::InputManagerApp input_manager_app;
  return mojo::RunApplication(application_request, &input_manager_app);
}
