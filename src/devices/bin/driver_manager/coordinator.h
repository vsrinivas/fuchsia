// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_COORDINATOR_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_COORDINATOR_H_

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <fidl/fuchsia.driver.development/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.driver.registrar/cpp/wire.h>
#include <fidl/fuchsia.fshost/cpp/wire.h>
#include <fidl/fuchsia.hardware.power.statecontrol/cpp/wire.h>
#include <fidl/fuchsia.power.manager/cpp/wire.h>
#include <lib/async/cpp/wait.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/device.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/stdcompat/optional.h>
#include <lib/svc/outgoing.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <memory>
#include <string_view>
#include <utility>

#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/string.h>
#include <fbl/vector.h>

#include "lib/async/dispatcher.h"
#include "src/devices/bin/driver_manager/bind_driver_manager.h"
#include "src/devices/bin/driver_manager/debug_dump.h"
#include "src/devices/bin/driver_manager/devfs.h"
#include "src/devices/bin/driver_manager/device.h"
#include "src/devices/bin/driver_manager/driver.h"
#include "src/devices/bin/driver_manager/driver_host.h"
#include "src/devices/bin/driver_manager/driver_loader.h"
#include "src/devices/bin/driver_manager/inspect.h"
#include "src/devices/bin/driver_manager/metadata.h"
#include "src/devices/bin/driver_manager/package_resolver.h"
#include "src/devices/bin/driver_manager/system_state_manager.h"
#include "src/devices/bin/driver_manager/v1/device_manager.h"
#include "src/devices/bin/driver_manager/v1/firmware_loader.h"
#include "src/devices/bin/driver_manager/v1/init_task.h"
#include "src/devices/bin/driver_manager/v1/resume_task.h"
#include "src/devices/bin/driver_manager/v1/suspend_resume_manager.h"
#include "src/devices/bin/driver_manager/v1/suspend_task.h"
#include "src/devices/bin/driver_manager/v1/unbind_task.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

namespace statecontrol_fidl = fuchsia_hardware_power_statecontrol;
using statecontrol_fidl::wire::SystemPowerState;
namespace fdf = fuchsia_driver_framework;

class BindDriverManager;
class DebugDump;
class DeviceManager;
class DriverHostLoaderService;
class FirmwareLoader;
class FsProvider;
class SuspendResumeManager;
class SystemStateManager;

constexpr zx::duration kDefaultResumeTimeout = zx::sec(30);
constexpr zx::duration kDefaultSuspendTimeout = zx::sec(30);

using ResumeCallback = std::function<void(zx_status_t)>;

// The action to take when we witness a driver host crash.
enum class DriverHostCrashPolicy {
  // Restart the driver host, with exponential backoff, up to 3 times.
  // This will only be triggered if the driver host which host's the driver which created the
  // parent device being bound to doesn't also crash.
  // TODO(fxbug.dev/66442): Handle composite devices better (they don't seem to restart with this
  // policy set).
  kRestartDriverHost,
  // Reboot the system via the power manager.
  kRebootSystem,
  // Don't take any action, other than cleaning up some internal driver manager state.
  kDoNothing,
};

struct CoordinatorConfig {
  // Initial root resource from the kernel.
  zx::resource root_resource;
  // Job for all driver_hosts.
  zx::job driver_host_job;
  // Event that is signaled by the kernel in OOM situation.
  zx::event oom_event;
  // Client for the Arguments service.
  fidl::WireSyncClient<fuchsia_boot::Arguments>* boot_args;
  // Client for the DriverIndex.
  fidl::WireSharedClient<fdf::DriverIndex> driver_index;
  // Whether we require /system.
  bool require_system = false;
  // Whether to output logs to debuglog.
  bool log_to_debuglog = false;
  // Whether to enable verbose logging.
  bool verbose = false;
  // Whether to allow loading drivers ephemerally. This should only be enabled on eng builds.
  bool enable_ephemeral = false;
  // Timeout for system wide suspend
  zx::duration suspend_timeout = kDefaultSuspendTimeout;
  // Timeout for system wide resume
  zx::duration resume_timeout = kDefaultResumeTimeout;
  // System will be transitioned to this system power state during
  // component shutdown.
  SystemPowerState default_shutdown_system_state = SystemPowerState::kReboot;
  // Something to clone a handle from the environment to pass to a Devhost.
  FsProvider* fs_provider = nullptr;
  // The path prefix to find binaries, drivers, etc. Typically this is "/boot/", but in test
  // environments this might be different.
  std::string path_prefix = "/boot/";
  // The decision to make when we encounter a driver host crash.
  DriverHostCrashPolicy crash_policy = DriverHostCrashPolicy::kRestartDriverHost;
};

class Coordinator : public fidl::WireServer<fuchsia_driver_development::DriverDevelopment>,
                    public fidl::WireServer<fuchsia_device_manager::Administrator>,
                    public fidl::WireServer<fuchsia_driver_registrar::DriverRegistrar> {
 public:
  Coordinator(const Coordinator&) = delete;
  Coordinator& operator=(const Coordinator&) = delete;
  Coordinator(Coordinator&&) = delete;
  Coordinator& operator=(Coordinator&&) = delete;

  Coordinator(CoordinatorConfig config, InspectManager* inspect_manager,
              async_dispatcher_t* dispatcher, async_dispatcher_t* firmware_dispatcher);
  ~Coordinator();

  zx_status_t InitOutgoingServices(const fbl::RefPtr<fs::PseudoDir>& svc_dir);
  zx::status<> PublishDriverDevelopmentService(const fbl::RefPtr<fs::PseudoDir>& svc_dir);

  // Initialization functions for DFv1. InitCoreDevices() is public for testing only.
  void LoadV1Drivers(std::string_view sys_device_driver,
                     fbl::Vector<std::string>& driver_search_paths,
                     fbl::Vector<const char*>& load_drivers);
  void InitCoreDevices(std::string_view sys_device_driver);
  void DriverAddedInit(Driver* drv, const char* version);

  // Start searching the system for non-boot drivers.
  // This will start a new thread to load non-boot drivers asynchronously.
  void StartLoadingNonBootDrivers();

  void BindFallbackDrivers();
  void AddAndBindDrivers(fbl::DoublyLinkedList<std::unique_ptr<Driver>> drivers);
  void DriverAdded(Driver* drv, const char* version);

  zx_status_t LibnameToVmo(const fbl::String& libname, zx::vmo* out_vmo) const;
  const Driver* LibnameToDriver(std::string_view libname) const;

  zx_status_t MakeVisible(const fbl::RefPtr<Device>& dev);

  zx_status_t GetTopologicalPath(const fbl::RefPtr<const Device>& dev, char* out, size_t max) const;

  zx_status_t GetMetadata(const fbl::RefPtr<Device>& dev, uint32_t type, void* buffer,
                          size_t buflen, size_t* size);
  zx_status_t GetMetadataSize(const fbl::RefPtr<Device>& dev, uint32_t type, size_t* size) {
    return GetMetadata(dev, type, nullptr, 0, size);
  }
  zx_status_t AddMetadata(const fbl::RefPtr<Device>& dev, uint32_t type, const void* data,
                          uint32_t length);

  zx_status_t PrepareProxy(const fbl::RefPtr<Device>& dev,
                           fbl::RefPtr<DriverHost> target_driver_host);
  zx_status_t PrepareNewProxy(const fbl::RefPtr<Device>& dev,
                              fbl::RefPtr<DriverHost> target_driver_host);

  async_dispatcher_t* dispatcher() const { return dispatcher_; }
  const zx::resource& root_resource() const { return config_.root_resource; }
  zx::duration resume_timeout() const { return config_.resume_timeout; }
  fidl::WireSyncClient<fuchsia_boot::Arguments>* boot_args() const { return config_.boot_args; }
  SystemPowerState shutdown_system_state() const { return shutdown_system_state_; }
  SystemPowerState default_shutdown_system_state() const {
    return config_.default_shutdown_system_state;
  }
  void set_shutdown_system_state(SystemPowerState state) { shutdown_system_state_ = state; }

  void set_running(bool running) { running_ = running; }
  void set_power_manager_registered(bool registered) { power_manager_registered_ = registered; }
  bool power_manager_registered() { return power_manager_registered_; }

  void set_loader_service_connector(LoaderServiceConnector loader_service_connector) {
    loader_service_connector_ = std::move(loader_service_connector);
  }

  void set_system_state_manager(std::unique_ptr<SystemStateManager> system_state_mgr) {
    system_state_manager_ = std::move(system_state_mgr);
  }

  fbl::DoublyLinkedList<std::unique_ptr<Driver>>& drivers() { return drivers_; }
  const fbl::DoublyLinkedList<std::unique_ptr<Driver>>& drivers() const { return drivers_; }

  // Called when a new driver becomes available to the Coordinator. Existing devices are
  // inspected to see if the new driver is bindable to them (unless they are already bound).
  // This method is public only for the test suite.
  zx_status_t BindDriver(Driver* drv);

  // Callback function to attempt binding a driver to the device.
  // TODO(fxb/90932): Remove this callback, as it makes things more complex and is only useful
  // for testing.
  zx_status_t AttemptBind(const Driver* drv, const fbl::RefPtr<Device>& dev);

  // This method is public only for the LoadDriverPackageTest.
  zx_status_t LoadEphemeralDriver(internal::PackageResolverInterface* resolver,
                                  const std::string& package_url);

  // These methods are used by the DriverHost class to register in the coordinator's bookkeeping
  void RegisterDriverHost(DriverHost* dh) { driver_hosts_.push_back(dh); }
  void UnregisterDriverHost(DriverHost* dh) { driver_hosts_.erase(*dh); }

  // Returns URL to driver that should be bound to fragments of composite devices.
  std::string GetFragmentDriverUrl() const;

  using RegisterWithPowerManagerCompletion = fit::callback<void(zx_status_t)>;
  void RegisterWithPowerManager(fidl::ClientEnd<fuchsia_io::Directory> devfs,
                                RegisterWithPowerManagerCompletion completion);
  void RegisterWithPowerManager(
      fidl::ClientEnd<fuchsia_power_manager::DriverManagerRegistration> power_manager,
      fidl::ClientEnd<fuchsia_device_manager::SystemStateTransition> system_state_transition,
      fidl::ClientEnd<fuchsia_io::Directory> devfs, RegisterWithPowerManagerCompletion completion);

  const fbl::RefPtr<Device>& root_device() { return root_device_; }
  const fbl::RefPtr<Device>& sys_device() { return sys_device_; }

  zx_status_t SetMexecZbis(zx::vmo kernel_zbi, zx::vmo data_zbi);

  SuspendResumeManager* suspend_resume_manager() { return suspend_resume_manager_.get(); }

  const Driver* fragment_driver() { return driver_loader_.LoadDriverUrl(GetFragmentDriverUrl()); }

  InspectManager& inspect_manager() { return *inspect_manager_; }
  DriverLoader& driver_loader() { return driver_loader_; }

  DeviceManager* device_manager() const { return device_manager_.get(); }

  BindDriverManager* bind_driver_manager() const { return bind_driver_manager_.get(); }

  FirmwareLoader* firmware_loader() const { return firmware_loader_.get(); }

  // Only exposed for testing.
  const DebugDump* debug_dump() const { return debug_dump_.get(); }

  const zx::vmo& mexec_kernel_zbi() const { return mexec_kernel_zbi_; }
  const zx::vmo& mexec_data_zbi() const { return mexec_data_zbi_; }

 private:
  // fuchsia.driver.development/DriverDevelopment interface
  void RestartDriverHosts(RestartDriverHostsRequestView request,
                          RestartDriverHostsCompleter::Sync& completer) override;
  void GetDriverInfo(GetDriverInfoRequestView request,
                     GetDriverInfoCompleter::Sync& completer) override;
  void GetDeviceInfo(GetDeviceInfoRequestView request,
                     GetDeviceInfoCompleter::Sync& completer) override;

  // fuchsia.device.manager/Administrator interface
  void Suspend(SuspendRequestView request, SuspendCompleter::Sync& completer) override;
  void UnregisterSystemStorageForShutdown(
      UnregisterSystemStorageForShutdownRequestView request,
      UnregisterSystemStorageForShutdownCompleter::Sync& completer) override;

  zx::status<std::vector<fuchsia_driver_development::wire::DriverInfo>> GetDriverInfo(
      fidl::AnyArena& allocator, const std::vector<const Driver*>& drivers);
  zx::status<std::vector<fuchsia_driver_development::wire::DeviceInfo>> GetDeviceInfo(
      fidl::AnyArena& allocator, const std::vector<fbl::RefPtr<Device>>& devices);

  // Driver registrar interface
  void Register(RegisterRequestView request, RegisterCompleter::Sync& completer) override;

  void OnOOMEvent(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                  const zx_packet_signal_t* signal);
  async::WaitMethod<Coordinator, &Coordinator::OnOOMEvent> wait_on_oom_event_{this};

  zx_status_t NewDriverHost(const char* name, fbl::RefPtr<DriverHost>* out);

  CoordinatorConfig config_;
  async_dispatcher_t* const dispatcher_;
  bool running_ = false;
  bool launched_first_driver_host_ = false;
  bool power_manager_registered_ = false;
  LoaderServiceConnector loader_service_connector_;
  fidl::WireSharedClient<fuchsia_power_manager::DriverManagerRegistration> power_manager_client_;

  internal::BasePackageResolver base_resolver_;
  DriverLoader driver_loader_;

  // All Drivers
  fbl::DoublyLinkedList<std::unique_ptr<Driver>> drivers_;

  // Drivers to try last
  fbl::DoublyLinkedList<std::unique_ptr<Driver>> fallback_drivers_;

  // All DriverHosts
  fbl::DoublyLinkedList<DriverHost*> driver_hosts_;

  fbl::RefPtr<Device> root_device_;
  fbl::RefPtr<Device> sys_device_;

  InspectManager* const inspect_manager_;
  std::unique_ptr<SystemStateManager> system_state_manager_;
  SystemPowerState shutdown_system_state_;

  cpp17::optional<fidl::ServerBindingRef<fuchsia_driver_registrar::DriverRegistrar>>
      driver_registrar_binding_;
  internal::PackageResolver package_resolver_;

  std::unique_ptr<DebugDump> debug_dump_;

  std::unique_ptr<FirmwareLoader> firmware_loader_;

  // Stashed mexec inputs.
  zx::vmo mexec_kernel_zbi_, mexec_data_zbi_;

  std::unique_ptr<SuspendResumeManager> suspend_resume_manager_;

  std::unique_ptr<DeviceManager> device_manager_;

  std::unique_ptr<BindDriverManager> bind_driver_manager_;
};

bool can_driver_bind(const Driver* drv, uint32_t protocol_id,
                     const fbl::Array<const zx_device_prop_t>& props,
                     const fbl::Array<const StrProperty>& str_props, bool autobind);

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_COORDINATOR_H_
