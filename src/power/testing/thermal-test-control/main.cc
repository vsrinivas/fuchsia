// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include "thermal_test_control.h"

int main(int argc, const char** argv) {
  syslog::SetTags({"thermal_test_control"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  ThermalTestControl app(std::move(context));

  FX_LOGS(INFO) << "Thermal test control is now running";

  return loop.Run();
}
