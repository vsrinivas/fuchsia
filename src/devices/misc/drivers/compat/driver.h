// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MISC_DRIVERS_COMPAT_DRIVER_H_
#define SRC_DEVICES_MISC_DRIVERS_COMPAT_DRIVER_H_

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/scope.h>
#include <lib/service/llcpp/outgoing_directory.h>

#include "src/devices/lib/driver2/devfs_exporter.h"
#include "src/devices/lib/driver2/logger.h"
#include "src/devices/lib/driver2/namespace.h"
#include "src/devices/misc/drivers/compat/device.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

namespace compat {

// Driver is the compatibility driver that loads DFv1 drivers.
class Driver {
 public:
  Driver(async_dispatcher_t* dispatcher,
         fidl::WireSharedClient<fuchsia_driver_framework::Node> node, driver::Namespace ns,
         driver::Logger logger, std::string_view url, std::string_view name, void* context,
         const zx_protocol_device_t* ops, std::optional<Device*> linked_device);
  ~Driver();

  zx_driver_t* ZxDriver();

  static constexpr const char* Name() { return "compat"; }

  static zx::status<std::unique_ptr<Driver>> Start(
      fuchsia_driver_framework::wire::DriverStartArgs& start_args, async_dispatcher_t* dispatcher,
      fidl::WireSharedClient<fuchsia_driver_framework::Node> node, driver::Namespace ns,
      driver::Logger logger);

  // Returns the context that DFv1 driver provided.
  void* Context() const;
  // Logs a message for the DFv1 driver.
  void Log(FuchsiaLogSeverity severity, const char* tag, const char* file, int line,
           const char* msg, va_list args);

  zx_status_t AddDevice(Device* parent, device_add_args_t* args, zx_device_t** out);

 private:
  // Run the driver at `driver_path`.
  zx::status<> Run(fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir,
                   std::string_view driver_path);

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

  fpromise::promise<void, zx_status_t> ConnectToParentCompatService();
  fpromise::promise<std::string, zx_status_t> GetTopologicalPath();

  fidl::WireSharedClient<fuchsia_driver_compat::Device> device_client_;

  fbl::RefPtr<fs::PseudoDir> compat_service_;
  async_dispatcher_t* dispatcher_;
  async::Executor executor_;
  service::OutgoingDirectory outgoing_;

  const driver::Namespace ns_;
  driver::Logger logger_;

  const std::string url_;
  Device device_;

  // When this driver creates a new Device, that Device's protocol will get this id number.
  uint32_t next_device_id_ = 0;

  void* library_ = nullptr;
  zx_driver_rec_t* record_ = nullptr;
  void* context_ = nullptr;

  // API resources.
  driver::Logger inner_logger_;
  zx::resource root_resource_;

  driver::DevfsExporter exporter_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace compat

struct zx_driver : public compat::Driver {
  // NOTE: Intentionally empty, do not add to this.
};

extern std::mutex kRootResourceLock;
extern zx::resource kRootResource __TA_GUARDED(kRootResourceLock);

#endif  // SRC_DEVICES_MISC_DRIVERS_COMPAT_DRIVER_H_
