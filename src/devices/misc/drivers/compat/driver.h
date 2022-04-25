// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MISC_DRIVERS_COMPAT_DRIVER_H_
#define SRC_DEVICES_MISC_DRIVERS_COMPAT_DRIVER_H_

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.scheduler/cpp/markers.h>
#include <lib/async/cpp/executor.h>
#include <lib/driver2/devfs_exporter.h>
#include <lib/driver2/logger.h>
#include <lib/driver2/namespace.h>
#include <lib/fpromise/scope.h>

#include <unordered_set>

#include "src/devices/lib/compat/compat.h"
#include "src/devices/misc/drivers/compat/device.h"
#include "src/devices/misc/drivers/compat/sysmem.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

namespace compat {

// Driver is the compatibility driver that loads DFv1 drivers.
class Driver {
 public:
  Driver(async_dispatcher_t* dispatcher,
         fidl::WireSharedClient<fuchsia_driver_framework::Node> node, driver::Namespace ns,
         driver::Logger logger, std::string_view url, device_t device,
         const zx_protocol_device_t* ops, component::OutgoingDirectory outgoing);
  ~Driver();

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

  zx::status<zx::vmo> LoadFirmware(Device* device, const char* filename, size_t* size);
  void LoadFirmwareAsync(Device* device, const char* filename, load_firmware_callback_t callback,
                         void* ctx);

  zx_status_t AddDevice(Device* parent, device_add_args_t* args, zx_device_t** out);
  zx::status<zx::profile> GetSchedulerProfile(uint32_t priority, const char* name);
  zx::status<zx::profile> GetDeadlineProfile(uint64_t capacity, uint64_t deadline, uint64_t period,
                                             const char* name);

  Device& GetDevice() { return device_; }
  const driver::Namespace& driver_namespace() { return ns_; }
  async_dispatcher_t* dispatcher() { return dispatcher_; }
  Sysmem& sysmem() { return sysmem_; }
  driver::Logger& logger() { return logger_; }

 private:
  // Run the driver at `driver_path`.
  zx::status<> Run(fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir,
                   std::string_view driver_path);

  // Gets the root resource for the DFv1 driver.
  fpromise::promise<zx::resource, zx_status_t> GetRootResource(
      const fidl::WireSharedClient<fuchsia_boot::RootResource>& root_resource);

  struct FileVmo {
    zx::vmo vmo;
    size_t size;
  };
  // Gets the underlying buffer for a given file.
  fpromise::promise<FileVmo, zx_status_t> GetBuffer(
      const fidl::WireSharedClient<fuchsia_io::File>& file);
  // Joins the results of getting the root resource, as well as the getting the
  // buffers for the compatibility driver and DFv1 driver.
  fpromise::result<std::tuple<zx::vmo, zx::vmo>, zx_status_t> Join(
      fpromise::result<std::tuple<fpromise::result<zx::resource, zx_status_t>,
                                  fpromise::result<FileVmo, zx_status_t>,
                                  fpromise::result<FileVmo, zx_status_t>>>& results);
  // Loads the driver using the provided `vmos`.
  fpromise::result<void, zx_status_t> LoadDriver(std::tuple<zx::vmo, zx::vmo>& vmos);
  // Starts the DFv1 driver.
  fpromise::result<void, zx_status_t> StartDriver();
  // Stops the DFv1 driver if there was a failure.
  fpromise::result<> StopDriver(const zx_status_t& status);

  fpromise::promise<void, zx_status_t> GetDeviceInfo();

  fbl::RefPtr<fs::PseudoDir> compat_service_;
  async_dispatcher_t* const dispatcher_;
  async::Executor executor_;
  component::OutgoingDirectory outgoing_;
  Interop interop_;

  const driver::Namespace ns_;
  driver::Logger logger_;

  const std::string url_;
  driver::Logger inner_logger_;
  Device device_;
  // TODO(fxbug.dev/93333): remove this once we have proper composite support.
  Sysmem sysmem_;

  void* library_ = nullptr;
  zx_driver_rec_t* record_ = nullptr;
  void* context_ = nullptr;

  // API resources.
  zx::resource root_resource_;

  fidl::WireSharedClient<fuchsia_driver_compat::Device> parent_client_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

class DriverList {
 public:
  zx_driver_t* ZxDriver();

  // Add a driver to the list. `driver` is unowned, and needs
  // to be removed before the driver goes out of scope.
  void AddDriver(Driver* driver);
  void RemoveDriver(Driver* driver);

  // Logs a message for the DFv1 driver.
  void Log(FuchsiaLogSeverity severity, const char* tag, const char* file, int line,
           const char* msg, va_list args);

 private:
  std::unordered_set<Driver*> drivers_;
};

// This is the list of all of the compat drivers loaded in the same
// driver host.
extern DriverList global_driver_list;

}  // namespace compat

struct zx_driver : public compat::DriverList {
  // NOTE: Intentionally empty, do not add to this.
};

extern std::mutex kRootResourceLock;
extern zx::resource kRootResource __TA_GUARDED(kRootResourceLock);

#endif  // SRC_DEVICES_MISC_DRIVERS_COMPAT_DRIVER_H_
