// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include <iostream>

#include "src/sys/tools/log/log.h"

int main(int argc, char** argv) {
  auto time = zx::clock::get_monotonic();

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  fuchsia::logger::LogSinkHandle log_sink;
  zx_status_t status = context->svc()->Connect(log_sink.NewRequest());
  if (status != ZX_OK) {
    std::cerr << "Failed to request service." << std::endl;
    return EXIT_FAILURE;
  }

  status = log::ParseAndWriteLog(std::move(log_sink), std::move(time), argc, argv);
  if (status != ZX_OK) {
    return EXIT_FAILURE;
  }

  return loop.RunUntilIdle();
}
