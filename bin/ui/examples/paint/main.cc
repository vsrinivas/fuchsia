// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-provider/provider.h>

#include "apps/mozart/examples/paint/paint_view.h"
#include "apps/mozart/lib/view_framework/view_provider_app.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  trace::TraceProvider trace_provider(loop.async());

  mozart::ViewProviderApp app([](mozart::ViewContext view_context) {
    return std::make_unique<examples::PaintView>(
        std::move(view_context.view_manager),
        std::move(view_context.view_owner_request));
  });

  loop.Run();
  return 0;
}
