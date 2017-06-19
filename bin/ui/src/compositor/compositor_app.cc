// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/compositor_app.h"

#include "apps/mozart/src/compositor/compositor_impl.h"
#include "apps/tracing/lib/trace/provider.h"
#include "lib/ftl/logging.h"

namespace compositor {

constexpr char kCompositorConfigFile[] = "/pkg/data/compositor.config";

CompositorApp::CompositorApp()
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()) {
  FTL_DCHECK(application_context_);

  if (!config_.ReadFrom(kCompositorConfigFile))
    FTL_LOG(WARNING) << "Could not parse " << kCompositorConfigFile;

  engine_ = std::make_unique<CompositorEngine>(&config_);

  tracing::InitializeTracer(application_context_.get(), {"compositor"});
  tracing::SetDumpCallback([this](std::unique_ptr<tracing::Dump> dump) {
    engine_->Dump(std::move(dump));
  });

  application_context_->outgoing_services()->AddService<mozart::Compositor>(
      [this](fidl::InterfaceRequest<mozart::Compositor> request) {
        compositor_bindings_.AddBinding(
            std::make_unique<CompositorImpl>(engine_.get()),
            std::move(request));
      });
}

CompositorApp::~CompositorApp() {
  tracing::SetDumpCallback(tracing::DumpCallback());
}

}  // namespace compositor
