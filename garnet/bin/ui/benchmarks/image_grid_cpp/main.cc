// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/trace-provider/provider.h>
#include <lib/ui/base_view/cpp/view_provider_component_transitional.h>

#include "garnet/bin/ui/benchmarks/image_grid_cpp/image_grid_view.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  scenic::ViewProviderComponentTransitional component(
      [](scenic::ViewContextTransitional view_context) {
        return std::make_unique<image_grid::ImageGridView>(std::move(view_context));
      },
      &loop);

  loop.Run();
  return 0;
}
