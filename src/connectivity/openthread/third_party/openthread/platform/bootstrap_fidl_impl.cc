// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootstrap_fidl_impl.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <src/lib/files/file.h>
#include <src/lib/fsl/vmo/strings.h>

#include "thread_config_manager.h"

namespace ot {
namespace Fuchsia {
namespace {
constexpr char kMigrationConfigPath[] = "/config/data/migration_config.json";
}  // namespace

// BootstrapImpl definitions -------------------------------------------------------

BootstrapImpl::BootstrapImpl(sys::ComponentContext* context) : context_(context) {}

BootstrapImpl::~BootstrapImpl() {
  if (serving_) {
    StopServingFidl();
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

void BootstrapImpl::StopServingFidl() {
  // Stop serving this FIDL and close all active bindings.
  auto status = context_->outgoing()->RemovePublicService<fuchsia::thread::Bootstrap>();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not remove service from outgoing directory.";
  } else {
    serving_ = false;
  }
}

void BootstrapImpl::StopServingFidlAndCloseBindings(zx_status_t close_bindings_status) {
  StopServingFidl();
  bindings_.CloseAll(close_bindings_status);
}

void BootstrapImpl::ImportThreadSettings(fuchsia::mem::Buffer thread_settings_json,
                                         ImportThreadSettingsCallback callback) {
  std::string data;

  if (!fsl::StringFromVmo(thread_settings_json, &data)) {
    FX_LOGS(ERROR) << "Failed to get data from VMO.";
    StopServingFidlAndCloseBindings(ZX_ERR_IO);
    return;
  }

  if (!files::WriteFile(GetSettingsPath(), data.data(), data.size())) {
    FX_LOGS(ERROR) << "Failed to write data to internal config location";
    StopServingFidlAndCloseBindings(ZX_ERR_IO);
    return;
  }

  callback();
  StopServingFidlAndCloseBindings(ZX_OK);
}

bool BootstrapImpl::ShouldServe() { return files::IsFile(kMigrationConfigPath); }

std::string BootstrapImpl::GetSettingsPath() { return kThreadSettingsPath; }

}  // namespace Fuchsia
}  // namespace ot
