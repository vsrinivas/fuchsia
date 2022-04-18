// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/driver_registrar_service.h"

#include "src/devices/lib/log/log.h"
#include "src/lib/storage/vfs/cpp/service.h"

namespace fdr = fuchsia_driver_registrar;

namespace driver_manager {

DriverRegistrarService::DriverRegistrarService(
    async_dispatcher_t* dispatcher, fidl::ClientEnd<fdr::DriverRegistrar> driver_registrar)
    : dispatcher_(dispatcher), driver_registrar_(std::move(driver_registrar)) {}

zx::status<> DriverRegistrarService::Publish(const fbl::RefPtr<fs::PseudoDir>& svc_dir) {
  const auto service = [this](fidl::ServerEnd<fdr::DriverRegistrar> request) {
    fidl::BindServer<fidl::WireServer<fdr::DriverRegistrar>>(this->dispatcher_, std::move(request),
                                                             this);
    return ZX_OK;
  };
  zx_status_t status = svc_dir->AddEntry(fidl::DiscoverableProtocolName<fdr::DriverRegistrar>,
                                         fbl::MakeRefCounted<fs::Service>(service));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to add directory entry '%s': %s",
         fidl::DiscoverableProtocolName<fdr::DriverRegistrar>, zx_status_get_string(status));
  }

  return zx::make_status(status);
}

void DriverRegistrarService::Register(RegisterRequestView request,
                                      RegisterCompleter::Sync& completer) {
  auto resp = this->driver_registrar_->Register(request->package_url);

  if (!resp.ok()) {
    LOGF(ERROR, "Driver register failed, error: %s", resp.error().FormatDescription().c_str());
    completer.ReplyError(resp.error().status());
    return;
  }

  if (resp->result.is_err()) {
    completer.ReplyError(resp->result.err());
  } else {
    completer.ReplySuccess();
  }
}
}  // namespace driver_manager
