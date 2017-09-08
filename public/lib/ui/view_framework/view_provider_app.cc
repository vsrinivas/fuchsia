// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/view_framework/view_provider_app.h"

namespace mozart {

ViewProviderApp::ViewProviderApp(ViewFactory factory)
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
      service_(application_context_.get(), std::move(factory)) {
}

ViewProviderApp::~ViewProviderApp() {}

}  // namespace mozart
