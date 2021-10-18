// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MISC_DRIVERS_COMPAT_DRIVER_H_
#define SRC_DEVICES_MISC_DRIVERS_COMPAT_DRIVER_H_

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/scope.h>
#include <lib/service/llcpp/outgoing_directory.h>

#include "src/devices/lib/driver2/logger.h"
#include "src/devices/lib/driver2/namespace.h"
#include "src/devices/misc/drivers/compat/device.h"

namespace compat {

// Driver is the compatibility driver that loads DFv1 drivers.
class Driver {
 public:
  Driver(const char* name, void* context, const zx_protocol_device_t* ops,
         std::optional<Device*> parent, async_dispatcher_t* dispatcher);
  ~Driver();

  zx_driver_t* ZxDriver();

  // Starts the driver from `start_args`.
  zx::status<> Start(fuchsia_driver_framework::wire::DriverStartArgs* start_args);

  // Returns the context that DFv1 driver provided.
  void* Context() const;
  // Logs a message for the DFv1 driver.
  void Log(FuchsiaLogSeverity severity, const char* tag, const char* file, int line,
           const char* msg, va_list args);

 private:
  // Gets the root resource for the DFv1 driver.
  fpromise::promise<zx::resource, zx_status_t> GetRootResource(
      const fidl::WireSharedClient<fuchsia_boot::RootResource>& root_resource);
  // Gets the underlying buffer for a given file.
  fpromise::promise<zx::vmo, zx_status_t> GetBuffer(
      const fidl::WireSharedClient<fuchsia_io::File>& file);
  // Joins the results of getting the root resource, as well as the getting the
  // buffers for the compatibility driver and DFv1 driver.
  fpromise::result<std::tuple<zx::vmo, zx::vmo>, zx_status_t> Join(
      fpromise::result<std::tuple<fpromise::result<zx::resource, zx_status_t>,
                                  fpromise::result<zx::vmo, zx_status_t>,
                                  fpromise::result<zx::vmo, zx_status_t>>>& results);
  // Loads the driver using the provided `vmos`.
  fpromise::result<void, zx_status_t> LoadDriver(std::tuple<zx::vmo, zx::vmo>& vmos);
  // Starts the DFv1 driver.
  fpromise::result<void, zx_status_t> StartDriver();
  // Stops the DFv1 driver if there was a failure.
  fpromise::result<> StopDriver(const zx_status_t& status);

  async_dispatcher_t* dispatcher_;
  async::Executor executor_;
  service::OutgoingDirectory outgoing_;

  driver::Namespace ns_;
  driver::Logger logger_;

  Device device_;

  std::string url_;
  void* library_ = nullptr;
  zx_driver_rec_t* record_ = nullptr;
  void* context_ = nullptr;

  // API resources.
  driver::Logger inner_logger_;
  zx::resource root_resource_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace compat

struct zx_driver : public compat::Driver {
  // NOTE: Intentionally empty, do not add to this.
};

extern zx::resource kRootResource;

#endif  // SRC_DEVICES_MISC_DRIVERS_COMPAT_DRIVER_H_
