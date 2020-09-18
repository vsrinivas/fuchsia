// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include "src/ui/a11y/bin/a11y_manager/app.h"
#include "src/ui/a11y/lib/annotation/annotation_view.h"
#include "src/ui/a11y/lib/semantics/a11y_semantics_event_manager.h"
#include "src/ui/a11y/lib/view/a11y_view_semantics.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  auto inspector = std::make_unique<sys::ComponentInspector>(context.get());
  inspector->Health().StartingUp();
  inspector->Health().Ok();

  a11y::ViewManager view_manager(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                                 std::make_unique<a11y::A11yViewSemanticsFactory>(),
                                 std::make_unique<a11y::AnnotationViewFactory>(),
                                 std::make_unique<a11y::A11ySemanticsEventManager>(), context.get(),
                                 context->outgoing()->debug_dir());
  a11y::TtsManager tts_manager(context.get());
  a11y::ColorTransformManager color_transform_manager(context.get());
  a11y::GestureListenerRegistry gesture_listener_registry;

  a11y_manager::App app(context.get(), &view_manager, &tts_manager, &color_transform_manager,
                        &gesture_listener_registry, inspector->root().CreateChild("A11y Manager"));

  loop.Run();
  return 0;
}
