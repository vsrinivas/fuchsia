// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/trace-provider/provider.h>

#include "src/lib/ui/base_view/view_provider_component.h"
#include "src/ui/examples/spinning_square_view/spinning_square_view.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  scenic::ViewProviderComponent component(
      [](scenic::ViewContext context) {
        return std::make_unique<examples::SpinningSquareView>(std::move(context));
      },
      &loop);

  loop.Run();
  return 0;
}
