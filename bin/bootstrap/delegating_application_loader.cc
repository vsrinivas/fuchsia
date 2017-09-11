// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/bootstrap/delegating_application_loader.h"

#include "lib/app/cpp/connect.h"
#include "lib/url/gurl.h"

namespace bootstrap {

DelegatingApplicationLoader::DelegatingApplicationLoader(
    Config::ServiceMap delegates,
    app::ApplicationLauncher* delegate_launcher,
    app::ApplicationLoaderPtr fallback)
    : delegate_launcher_(delegate_launcher), fallback_(std::move(fallback)) {
  for (auto& pair : delegates) {
    auto& record = delegate_instances_[pair.second->url];
    record.launch_info = std::move(pair.second);
    delegates_by_scheme_[pair.first] = &record;
  }
}

DelegatingApplicationLoader::~DelegatingApplicationLoader() = default;

void DelegatingApplicationLoader::LoadApplication(
    const fidl::String& url,
    const ApplicationLoader::LoadApplicationCallback& callback) {
  const url::GURL gurl(url);
  if (gurl.is_valid()) {
    auto it = delegates_by_scheme_.find(gurl.scheme());
    if (it != delegates_by_scheme_.end()) {
      auto* record = it->second;
      if (!record->loader) {
        StartDelegate(record);
      }
      record->loader->LoadApplication(url, callback);
      return;
    }
  }

  fallback_->LoadApplication(url, callback);
}

void DelegatingApplicationLoader::StartDelegate(
    ApplicationLoaderRecord* record) {
  app::ServiceProviderPtr service_provider;
  auto dup_launch_info = app::ApplicationLaunchInfo::New();
  dup_launch_info->url = record->launch_info->url;
  dup_launch_info->arguments = record->launch_info->arguments.Clone();
  dup_launch_info->services = service_provider.NewRequest();
  delegate_launcher_->CreateApplication(std::move(dup_launch_info),
                                        record->controller.NewRequest());

  record->loader =
      app::ConnectToService<app::ApplicationLoader>(service_provider.get());
  record->loader.set_connection_error_handler([this, record] {
    // proactively kill the loader app entirely if its ApplicationLoader died on
    // us
    record->controller.reset();
  });
}

}  // namespace bootstrap
