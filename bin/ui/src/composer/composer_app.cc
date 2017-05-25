// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/composer/composer_app.h"

#include "apps/mozart/src/composer/composer_impl.h"
#include "apps/tracing/lib/trace/provider.h"
#include "lib/ftl/logging.h"

namespace mozart {
namespace composer {

ComposerApp::ComposerApp(Params* params)
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()) {
  tracing::InitializeTracer(application_context_.get(), {"composer"});

  application_context_->outgoing_services()->AddService<mozart2::Composer>(
      [this](fidl::InterfaceRequest<mozart2::Composer> request) {
        FTL_LOG(INFO) << "Accepting connection to new ComposerImpl";
        composer_bindings_.AddBinding(std::make_unique<ComposerImpl>(),
                                      std::move(request));
      });
}

ComposerApp::~ComposerApp() {}

}  // namespace composer
}  // namespace mozart
