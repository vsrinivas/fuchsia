// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/fidl/app_client.h"

namespace modular {

AppClientBase::AppClientBase(app::ApplicationLauncher* const launcher,
                             AppConfigPtr config) {
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->services = services_.NewRequest();
  launch_info->url = config->url;
  launch_info->arguments = config->args.Clone();

  launcher->CreateApplication(std::move(launch_info), app_.NewRequest());
}

AppClientBase::~AppClientBase() = default;

void AppClientBase::AppTerminate(const std::function<void()>& done) {
  auto called = std::make_shared<bool>(false);
  auto cont = [this, called, done] {
    if (*called) {
      return;
    }

    *called = true;

    app_.set_connection_error_handler([] {});
    app_.reset();
    services_.reset();

    ServiceReset();

    done();
  };

  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      cont, ftl::TimeDelta::FromSeconds(kAppClientTimeoutSeconds));

  ServiceTerminate(cont);
}

void AppClientBase::SetAppErrorHandler(const ftl::Closure& error_handler) {
  app_.set_connection_error_handler(error_handler);
}

void AppClientBase::ServiceTerminate(const std::function<void()>& done) {}

void AppClientBase::ServiceReset() {}

}  // namespace modular
