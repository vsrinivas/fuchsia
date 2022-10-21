// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "service-manager.h"

#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>

#include "src/developer/adb/third_party/adb/types.h"

namespace adb {

namespace {

std::unordered_map<std::string_view, std::string_view> services{
    {kShellService, "fuchsia-boot:///#meta/adb-shell.cm"},
    {kFfxService, "fuchsia-boot:///#meta/adb-ffx.cm"},
    {kFileSyncService, "fuchsia-boot:///#meta/adb-file-sync.cm"},
};

}  // namespace

zx_status_t ServiceManager::Init() {
  auto client_end = component::Connect<fuchsia_component::Realm>();
  if (client_end.is_error()) {
    FX_LOGS(ERROR) << "Error when connecting to Realm " << client_end.status_string();
    return client_end.error_value();
  }

  realm_proxy_.Bind(std::move(client_end.value()));
  return ZX_OK;
}

zx::result<fidl::ClientEnd<fuchsia_hardware_adb::Provider>> ServiceManager::CreateDynamicChild(
    std::string_view name) {
  if (services.find(name) == services.end()) {
    FX_LOGS(ERROR) << "Service " << name << " not supported";
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  fidl::Arena allocator;
  auto result =
      realm_proxy_->CreateChild({.name = "adb-services"},
                                fuchsia_component_decl::wire::Child::Builder(allocator)
                                    .name(name)
                                    .url(services[name])
                                    .startup(fuchsia_component_decl::wire::StartupMode::kLazy)
                                    .Build(),
                                fuchsia_component::wire::CreateChildArgs());
  if (!result.ok() || (result->is_error() &&
                       result->error_value() != fuchsia_component::Error::kInstanceAlreadyExists)) {
    auto status = result.ok() ? static_cast<int32_t>(result->error_value()) : ZX_ERR_INTERNAL;
    FX_LOGS(ERROR) << "Create child failed with " << status;
    return zx::error(status);
  }
  FX_LOGS(DEBUG) << "Dynamic child instance created.";

  return ConnectDynamicChild(name);
}

zx::result<fidl::ClientEnd<fuchsia_hardware_adb::Provider>> ServiceManager::ConnectDynamicChild(
    std::string_view name) {
  // Connect to SVC
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  auto result = realm_proxy_->OpenExposedDir(
      {.name = fidl::StringView::FromExternal(name), .collection = "adb-services"},
      std::move(endpoints->server));
  if (!result.ok() || result->is_error()) {
    auto status =
        result->is_error() ? static_cast<int32_t>(result->error_value()) : result.status();
    FX_LOGS(ERROR) << "OpenExposedDir failed " << status;
    return zx::error(status);
  }
  return component::ConnectAt<fuchsia_hardware_adb::Provider>(endpoints->client);
}

}  // namespace adb
