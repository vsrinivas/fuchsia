// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"

#include "garnet/examples/media/simple_sine/simple_sine.h"

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;

  auto application_context = app::ApplicationContext::CreateFromStartupInfo();
  examples::MediaApp media_app;
  media_app.Run(application_context.get());

  // We've set everything going. Wait for our message loop to return.
  loop.Run();

  return 0;
}
