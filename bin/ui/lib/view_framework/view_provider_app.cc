// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/view_framework/view_provider_app.h"

#include "lib/ftl/logging.h"

namespace mozart {

ViewProviderApp::ViewProviderApp(
    ViewFactory factory,
    const ftl::RefPtr<ftl::TaskRunner>& task_runner)
    : factory_(factory),
      application_context_(
          modular::ApplicationContext::CreateFromStartupInfo()),
      weak_ptr_factory_(this) {
  FTL_DCHECK(task_runner);

  task_runner->PostTask([weak = weak_ptr_factory_.GetWeakPtr()] {
    if (weak)
      weak->Start();
  });
}

ViewProviderApp::~ViewProviderApp() {}

void ViewProviderApp::Start() {
  FTL_DCHECK(!service_);

  service_.reset(
      new ViewProviderService(application_context_.get(), std::move(factory_)));
}

}  // namespace mozart
