// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/view_framework/view_provider_app.h"

namespace mozart {

ViewProviderApp::ViewProviderApp(ViewFactory factory)
    : startup_context_(component::StartupContext::CreateFromStartupInfo()),
      service_(startup_context_.get(), std::move(factory)) {}

ViewProviderApp::ViewProviderApp(component::StartupContext* startup_context,
                                 ViewFactory factory)
    : service_(startup_context, std::move(factory)) {}

ViewProviderApp::~ViewProviderApp() {}

}  // namespace mozart
