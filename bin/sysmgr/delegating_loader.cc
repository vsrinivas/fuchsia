// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/sysmgr/delegating_loader.h"

#include "lib/fidl/cpp/clone.h"
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

DelegatingLoader::DelegatingLoader(Config::ServiceMap delegates,
                                   fuchsia::sys::Launcher* delegate_launcher,
                                   fuchsia::sys::LoaderPtr fallback)
    : delegate_launcher_(delegate_launcher), fallback_(std::move(fallback)) {
  for (auto& pair : delegates) {
    auto& record = delegate_instances_[pair.second->url];
    record.launch_info = std::move(pair.second);
    delegates_by_scheme_[pair.first] = &record;
  }
}

DelegatingLoader::~DelegatingLoader() = default;

void DelegatingLoader::LoadComponent(fidl::StringPtr url,
                                     LoadComponentCallback callback) {
  std::string scheme = GetScheme(url);
  if (!scheme.empty()) {
    auto it = delegates_by_scheme_.find(scheme);
    if (it != delegates_by_scheme_.end()) {
      auto* record = it->second;
      if (!record->loader) {
        StartDelegate(record);
      }
      record->loader->LoadComponent(url, std::move(callback));
      return;
    }
  }

  fallback_->LoadComponent(url, std::move(callback));
}

void DelegatingLoader::StartDelegate(LoaderRecord* record) {
  component::Services services;
  fuchsia::sys::LaunchInfo dup_launch_info;
  dup_launch_info.url = record->launch_info->url;
  fidl::Clone(record->launch_info->arguments, &dup_launch_info.arguments);
  dup_launch_info.directory_request = services.NewRequest();
  delegate_launcher_->CreateComponent(std::move(dup_launch_info),
                                      record->controller.NewRequest());

  record->loader = services.ConnectToService<fuchsia::sys::Loader>();
  record->loader.set_error_handler([this, record] {
    // proactively kill the loader app entirely if its Loader died on
    // us
    record->controller.Unbind();
  });
}

}  // namespace sysmgr
