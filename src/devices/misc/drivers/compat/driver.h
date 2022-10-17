// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MISC_DRIVERS_COMPAT_DRIVER_H_
#define SRC_DEVICES_MISC_DRIVERS_COMPAT_DRIVER_H_

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.scheduler/cpp/markers.h>
#include <lib/async/cpp/executor.h>
#include <lib/driver2/devfs_exporter.h>
#include <lib/driver2/driver2_cpp.h>
#include <lib/driver_compat/compat.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fpromise/scope.h>

#include <unordered_set>

#include "src/devices/misc/drivers/compat/device.h"
#include "src/devices/misc/drivers/compat/sysmem.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace compat {

// Get the list of fragment names routed to this driver.
std::vector<std::string> GetFragmentNames(
    fuchsia_driver_framework::wire::DriverStartArgs& start_args);

// Driver is the compatibility driver that loads DFv1 drivers.
class Driver : public driver::DriverBase {
 public:
  Driver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher,
         device_t device, const zx_protocol_device_t* ops, std::string_view driver_path);
  ~Driver() override;

  zx::result<> Start() override;

  // Returns the context that DFv1 driver provided.
  void* Context() const;
  // Logs a message for the DFv1 driver.
  void Log(FuchsiaLogSeverity severity, const char* tag, const char* file, int line,
           const char* msg, va_list args);

  zx::result<zx::vmo> LoadFirmware(Device* device, const char* filename, size_t* size);
  void LoadFirmwareAsync(Device* device, const char* filename, load_firmware_callback_t callback,
                         void* ctx);

  zx_status_t AddDevice(Device* parent, device_add_args_t* args, zx_device_t** out);
  zx::result<zx::profile> GetSchedulerProfile(uint32_t priority, const char* name);
  zx::result<zx::profile> GetDeadlineProfile(uint64_t capacity, uint64_t deadline, uint64_t period,
                                             const char* name);
  zx::result<> SetProfileByRole(zx::unowned_thread thread, std::string_view role);
  zx::result<std::string> GetVariable(const char* name);

  // Export a device to devfs. If this returns success, the deferred callback will remove
  // the device from devfs when it goes out of scope.
  zx::result<fit::deferred_callback> ExportToDevfsSync(
      fuchsia_device_fs::wire::ExportOptions options, fbl::RefPtr<fs::Vnode> dev_node,
      std::string name, std::string_view topological_path, uint32_t proto_id);

  Device& GetDevice() { return device_; }
  Sysmem& sysmem() { return sysmem_; }
  const driver::DevfsExporter& devfs_exporter() const { return devfs_exporter_; }

  // These accessors are used by other classes in the compat driver so we want to expose
  // them publicly since they are protected in DriverBase.
  async_dispatcher_t* dispatcher() { return driver::DriverBase::dispatcher(); }
  const driver::Namespace& driver_namespace() { return *context().incoming(); }
  driver::OutgoingDirectory& outgoing() { return *context().outgoing(); }

  uint32_t GetNextDeviceId() { return next_device_id_++; }

 private:
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

  fpromise::promise<void, zx_status_t> ConnectToParentDevices();
  fpromise::promise<void, zx_status_t> GetDeviceInfo();

  async::Executor executor_;
  std::string driver_path_;

  // The vfs to serve nodes that we are putting into devfs.
  std::unique_ptr<fs::SynchronousVfs> devfs_vfs_;
  // The directory to store nodes that we are putting into devfs.
  fbl::RefPtr<fs::PseudoDir> devfs_dir_;
  driver::DevfsExporter devfs_exporter_;
  std::string node_name_;

  driver::Logger inner_logger_;
  Device device_;

  // The next unique device id for devices. Starts at 1 because `device_` has id zero.
  uint32_t next_device_id_ = 1;

  // TODO(fxbug.dev/93333): remove this once we have proper composite support.
  Sysmem sysmem_;

  void* library_ = nullptr;
  zx_driver_rec_t* record_ = nullptr;
  void* context_ = nullptr;

  // API resources.
  zx::resource root_resource_;

  fidl::WireSharedClient<fuchsia_driver_compat::Device> parent_client_;
  std::unordered_map<std::string, fidl::WireSharedClient<fuchsia_driver_compat::Device>>
      parent_clients_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

class DriverFactory {
 public:
  static zx::result<std::unique_ptr<driver::DriverBase>> CreateDriver(
      driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher);
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

extern std::mutex kDriverGlobalsLock;
extern zx::resource kRootResource __TA_GUARDED(kDriverGlobalsLock);

#endif  // SRC_DEVICES_MISC_DRIVERS_COMPAT_DRIVER_H_
