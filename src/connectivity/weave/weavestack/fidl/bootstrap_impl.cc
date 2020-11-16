// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootstrap_impl.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <src/lib/files/file.h>
#include <src/lib/fsl/vmo/strings.h>

#include "src/connectivity/weave/adaptation/weave_config_manager.h"

namespace weavestack {
namespace {
using fuchsia::weave::Bootstrap_ImportWeaveConfig_Response;
using fuchsia::weave::Bootstrap_ImportWeaveConfig_Result;

using nl::Weave::DeviceLayer::Internal::WeaveConfigManager;

constexpr char kMigrationConfigPath[] = "/config/data/migration_config.json";
}  // namespace

// BootstrapImpl definitions -------------------------------------------------------

BootstrapImpl::BootstrapImpl(sys::ComponentContext* context) : context_(context) {}

BootstrapImpl::~BootstrapImpl() {
  if (serving_) {
    context_->outgoing()->RemovePublicService<fuchsia::weave::Bootstrap>();
    serving_ = false;
  }
}

zx_status_t BootstrapImpl::Init() {
  if (!ShouldServe()) {
    return ZX_OK;
  }

  // Register with the context.
  zx_status_t status = context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to register BootstrapImpl handler with status = "
                   << zx_status_get_string(status);
  } else {
    serving_ = true;
  }
  return status;
}

void BootstrapImpl::ImportWeaveConfig(fuchsia::mem::Buffer config_json,
                                      ImportWeaveConfigCallback callback) {
  Bootstrap_ImportWeaveConfig_Result result;
  Bootstrap_ImportWeaveConfig_Response response;
  std::string data;

  // Read data in.
  if (!fsl::StringFromVmo(config_json, &data)) {
    FX_LOGS(ERROR) << "Failed to get data from VMO.";
    result.set_err(ZX_ERR_IO);
    callback(std::move(result));
    return;
  }

  // Write data out.
  if (!files::WriteFile(GetConfigPath(), data.data(), data.size())) {
    FX_LOGS(ERROR) << "Failed to write data to internal config location";
    result.set_err(ZX_ERR_IO);
    callback(std::move(result));
  }

  // Respond to the caller.
  result.set_response(response);
  callback(std::move(result));

  // Successful import, stop serving this FIDL and close all active bindings.
  zx_status_t status = context_->outgoing()->RemovePublicService<fuchsia::weave::Bootstrap>();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not remove service from outgoing directory.";
  } else {
    serving_ = false;
  }
  bindings_.CloseAll(ZX_OK);
}

bool BootstrapImpl::ShouldServe() { return files::IsFile(kMigrationConfigPath); }

std::string BootstrapImpl::GetConfigPath() { return WeaveConfigManager::kEnvironmentStorePath; }

}  // namespace weavestack
