// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/sysmgr/delegating_application_loader.h"

#include "lib/svc/cpp/services.h"

namespace sysmgr {
namespace {

std::string GetScheme(const std::string& url) {
  size_t pos = url.find(':');
  if (pos == std::string::npos)
    return std::string();
  return url.substr(0, pos);
}

}  // namespace

DelegatingApplicationLoader::DelegatingApplicationLoader(
    Config::ServiceMap delegates,
    component::ApplicationLauncher* delegate_launcher,
    component::ApplicationLoaderPtr fallback)
    : delegate_launcher_(delegate_launcher), fallback_(std::move(fallback)) {
  for (auto& pair : delegates) {
    auto& record = delegate_instances_[pair.second->url];
    record.launch_info = std::move(pair.second);
    delegates_by_scheme_[pair.first] = &record;
  }
}

DelegatingApplicationLoader::~DelegatingApplicationLoader() = default;

void DelegatingApplicationLoader::LoadApplication(
    const f1dl::StringPtr& url,
    const ApplicationLoader::LoadApplicationCallback& callback) {
  std::string scheme = GetScheme(url);
  if (!scheme.empty()) {
    auto it = delegates_by_scheme_.find(scheme);
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
  component::Services services;
  auto dup_launch_info = component::ApplicationLaunchInfo::New();
  dup_launch_info->url = record->launch_info->url;
  dup_launch_info->arguments = record->launch_info->arguments.Clone();
  dup_launch_info->directory_request = services.NewRequest();
  delegate_launcher_->CreateApplication(std::move(dup_launch_info),
                                        record->controller.NewRequest());

  record->loader = services.ConnectToService<component::ApplicationLoader>();
  record->loader.set_error_handler([this, record] {
    // proactively kill the loader app entirely if its ApplicationLoader died on
    // us
    record->controller.Unbind();
  });
}

}  // namespace sysmgr
