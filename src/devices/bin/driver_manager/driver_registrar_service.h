// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_REGISTRAR_SERVICE_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_REGISTRAR_SERVICE_H_

#include <fidl/fuchsia.driver.registrar/cpp/wire.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

namespace fdr = fuchsia_driver_registrar;

namespace driver_manager {

class DriverRegistrarService : public fidl::WireServer<fuchsia_driver_registrar::DriverRegistrar> {
 public:
  explicit DriverRegistrarService(async_dispatcher_t* dispatcher,
                                  fidl::ClientEnd<fdr::DriverRegistrar> driver_registrar);

  zx::status<> Publish(const fbl::RefPtr<fs::PseudoDir>& svc_dir);

 private:
  // fidl::WireServer<fuchsia_driver_registrar::DriverRegistrar>
  void Register(RegisterRequestView request, RegisterCompleter::Sync& completer) override;

  async_dispatcher_t* const dispatcher_;
  fidl::WireSyncClient<fdr::DriverRegistrar> driver_registrar_;
};

}  // namespace driver_manager

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_REGISTRAR_SERVICE_H_
