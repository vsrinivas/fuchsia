// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include <cstring>

#include "src/intl/intl_services/run.h"
#include "src/ui/a11y/bin/a11y_manager/app.h"
#include "src/ui/a11y/lib/annotation/annotation_view.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"
#include "src/ui/a11y/lib/semantics/a11y_semantics_event_manager.h"
#include "src/ui/a11y/lib/util/boot_info_manager.h"
#include "src/ui/a11y/lib/view/a11y_view_semantics.h"
#include "src/ui/a11y/lib/view/flatland_accessibility_view.h"
#include "src/ui/a11y/lib/view/gfx_accessibility_view.h"
#include "src/ui/a11y/lib/view/view_injector_factory.h"

namespace {

int run_a11y_manager(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto context = sys::ComponentContext::Create();
  auto inspector = std::make_unique<sys::ComponentInspector>(context.get());
  inspector->Health().StartingUp();
  inspector->Health().Ok();

  std::unique_ptr<a11y::ViewManager> view_manager;
  std::unique_ptr<a11y_manager::App> app;

  a11y::TtsManager tts_manager(context.get());
  a11y::ColorTransformManager color_transform_manager(context.get());
  a11y::GestureListenerRegistry gesture_listener_registry;
  a11y::BootInfoManager boot_info_manager(context.get());
  a11y::ScreenReaderContextFactory screen_reader_context_factory;

  // Block until we know which composition API to use.
  auto scenic = context->svc()->Connect<fuchsia::ui::scenic::Scenic>();
  scenic.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::ui::scenic::Scenic: " << zx_status_get_string(status);
  });

  scenic->UsesFlatland([&](bool flatland_enabled) {
    std::shared_ptr<a11y::AccessibilityViewInterface> a11y_view;

    if (flatland_enabled) {
      auto flatland_a11y_view = std::make_shared<a11y::FlatlandAccessibilityView>(
          context->svc()->Connect<fuchsia::ui::composition::Flatland>(),
          context->svc()->Connect<fuchsia::ui::composition::Flatland>());
      context->outgoing()->AddPublicService(flatland_a11y_view->GetHandler());
      a11y_view = std::move(flatland_a11y_view);
    } else {
      a11y_view = std::make_shared<a11y::GfxAccessibilityView>(context.get());
    }

    view_manager = std::make_unique<a11y::ViewManager>(
        std::make_unique<a11y::SemanticTreeServiceFactory>(
            inspector->root().CreateChild("semantic_trees")),
        std::make_unique<a11y::A11yViewSemanticsFactory>(),
        std::make_unique<a11y::AnnotationViewFactory>(),
        std::make_unique<a11y::ViewInjectorFactory>(),
        std::make_unique<a11y::A11ySemanticsEventManager>(), a11y_view, context.get());

    app = std::make_unique<a11y_manager::App>(
        context.get(), view_manager.get(), &tts_manager, &color_transform_manager,
        &gesture_listener_registry, &boot_info_manager, &screen_reader_context_factory,
        inspector->root().CreateChild("a11y_manager_app"), flatland_enabled);
    context->outgoing()->ServeFromStartupInfo();
  });

  loop.Run();

  FX_LOGS(INFO) << "A11y manager exiting";
  return 0;
}

}  // namespace

int main(int argc, const char** argv) {
  if (strcmp(argv[0], "/pkg/bin/intl_services") == 0) {
    // If the binary was started as intl_services, run only that part of it.
    exit(intl::serve_fuchsia_intl_services(argc, argv));
  }
  return run_a11y_manager(argc, argv);
}
