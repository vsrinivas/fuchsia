// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/view_framework/view_provider_app.h"

#include "apps/tracing/lib/trace/provider.h"

namespace mozart {

ViewProviderApp::ViewProviderApp(ViewFactory factory)
    : application_context_(
          modular::ApplicationContext::CreateFromStartupInfo()),
      service_(application_context_.get(), std::move(factory)) {
  tracing::InitializeTracer(application_context_.get(), "app", {});
}

ViewProviderApp::~ViewProviderApp() {}

}  // namespace mozart
