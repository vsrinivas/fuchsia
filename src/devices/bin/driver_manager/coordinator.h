// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_COORDINATOR_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_COORDINATOR_H_

#include <fuchsia/boot/llcpp/fidl.h>
#include <fuchsia/fshost/llcpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/svc/outgoing.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/vmo.h>

#include <memory>
#include <utility>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/string.h>
#include <fbl/vector.h>

#include "composite_device.h"
#include "devhost.h"
#include "device.h"
#include "driver.h"
#include "fbl/auto_lock.h"
#include "fuchsia/device/manager/llcpp/fidl.h"
#include "fuchsia/hardware/power/statecontrol/llcpp/fidl.h"
#include "init_task.h"
#include "inspect.h"
#include "metadata.h"
#include "resume_task.h"
#include "suspend_task.h"
#include "unbind_task.h"
#include "vmo_writer.h"

using llcpp::fuchsia::device::manager::SystemPowerState;

class DevhostLoaderService;
class FsProvider;

constexpr zx::duration kDefaultResumeTimeout = zx::sec(30);
constexpr zx::duration kDefaultSuspendTimeout = zx::sec(30);

class SuspendContext {
 public:
  enum class Flags : uint32_t {
    kRunning = 0u,
    kSuspend = 1u,
  };

  SuspendContext() = default;

  SuspendContext(Flags flags, uint32_t sflags) : flags_(flags), sflags_(sflags) {}

  ~SuspendContext() {}

  SuspendContext(SuspendContext&&) = default;
  SuspendContext& operator=(SuspendContext&&) = default;

  void set_task(fbl::RefPtr<SuspendTask> task) { task_ = std::move(task); }

  const SuspendTask* task() const { return task_.get(); }

  Flags flags() const { return flags_; }
  void set_flags(Flags flags) { flags_ = flags; }
  uint32_t sflags() const { return sflags_; }

 private:
  fbl::RefPtr<SuspendTask> task_;

  Flags flags_ = Flags::kRunning;

  // suspend flags
  uint32_t sflags_ = 0u;
};

// Tracks the global resume state that is currently in progress.
class ResumeContext {
 public:
  enum class Flags : uint32_t {
    kResume = 0u,
    kSuspended = 1u,
  };
  ResumeContext() = default;

  ResumeContext(Flags flags, SystemPowerState resume_state)
      : target_state_(resume_state), flags_(flags) {}

  ~ResumeContext() {}

  ResumeContext(ResumeContext&&) = default;
  ResumeContext& operator=(ResumeContext&&) = default;

  Flags flags() const { return flags_; }
  void set_flags(Flags flags) { flags_ = flags; }
  void push_pending_task(fbl::RefPtr<ResumeTask> task) {
    pending_resume_tasks_.push_back(std::move(task));
  }
  void push_completed_task(fbl::RefPtr<ResumeTask> task) {
    completed_resume_tasks_.push_back(std::move(task));
  }

  bool pending_tasks_is_empty() { return pending_resume_tasks_.is_empty(); }
  bool completed_tasks_is_empty() { return completed_resume_tasks_.is_empty(); }

  std::optional<fbl::RefPtr<ResumeTask>> take_pending_task(Device& dev) {
    for (size_t i = 0; i < pending_resume_tasks_.size(); i++) {
      if (&pending_resume_tasks_[i]->device() == &dev) {
        auto task = pending_resume_tasks_.erase(i);
        return std::move(task);
      }
    }
    return {};
  }

  void reset_completed_tasks() { completed_resume_tasks_.reset(); }

  SystemPowerState target_state() const { return target_state_; }

 private:
  fbl::Vector<fbl::RefPtr<ResumeTask>> pending_resume_tasks_;
  fbl::Vector<fbl::RefPtr<ResumeTask>> completed_resume_tasks_;
  SystemPowerState target_state_;
  Flags flags_ = Flags::kSuspended;
};

// Values parsed out of argv.  All paths described below are absolute paths.
struct DevmgrArgs {
  // Load drivers from these directories.  If this is empty, the default will
  // be used.
  fbl::Vector<const char*> driver_search_paths;
  // Load the drivers with these paths.  The specified drivers do not need to
  // be in directories in |driver_search_paths|.
  fbl::Vector<const char*> load_drivers;
  // Use this driver as the sys_device driver.  If nullptr, the default will
  // be used.
  const char* sys_device_driver = nullptr;
  // Select whether to launch a new svchost process, or to use the /svc provided through the
  // namespace when launching subprocesses (only used in integration tests).
  bool start_svchost = true;
  // Disables the block watcher if set to true. This can be used for testing purposes,
  // where it is not necessary to have the block watcher running.
  bool disable_block_watcher = false;
  // Disables the netsvc if set to true. This can be used for testing purposes,
  // where it is not necessary to have the netsvc running.
  bool disable_netsvc = false;
  // Connect the stdout and stderr file descriptors for this program to a
  // debuglog handle acquired with fuchsia.boot.WriteOnlyLog.
  bool log_to_debuglog = false;
};

struct CoordinatorConfig {
  // Initial root resource from the kernel.
  zx::resource root_resource;
  // Job for all devhosts.
  zx::job devhost_job;
  // Event that is signaled by the kernel in OOM situation.
  zx::event oom_event;
  // Async dispatcher for the coordinator.
  async_dispatcher_t* dispatcher;
  // Client for the Arguments service.
  llcpp::fuchsia::boot::Arguments::SyncClient* boot_args;
  // If true, netsvc is disabled and will not start.
  bool disable_netsvc;
  // Whether we require /system.
  bool require_system;
  // Whether we require ASan drivers.
  bool asan_drivers;
  // Whether to reboot the device when suspend does not finish on time.
  bool suspend_fallback;
  // Timeout for system wide suspend
  zx::duration suspend_timeout = kDefaultSuspendTimeout;
  // Timeout for system wide resume
  zx::duration resume_timeout = kDefaultResumeTimeout;
  // Something to clone a handle from the environment to pass to a Devhost.
  FsProvider* fs_provider;
};

using LoaderServiceConnector = fit::function<zx_status_t(zx::channel*)>;
using ResumeCallback = std::function<void(zx_status_t)>;
using SuspendCallback = fit::function<void(zx_status_t)>;

struct SuspendCallbackInfo : public fbl::RefCounted<SuspendCallbackInfo> {
  SuspendCallbackInfo(SuspendCallback callback) : callback(std::move(callback)) {}
  SuspendCallback callback;
};

class Coordinator : public llcpp::fuchsia::hardware::power::statecontrol::Admin::Interface,
                    public llcpp::fuchsia::device::manager::BindDebugger::Interface {
 public:
  Coordinator(const Coordinator&) = delete;
  Coordinator& operator=(const Coordinator&) = delete;
  Coordinator(Coordinator&&) = delete;
  Coordinator& operator=(Coordinator&&) = delete;

  explicit Coordinator(CoordinatorConfig config);
  ~Coordinator();

  zx_status_t InitOutgoingServices(const fbl::RefPtr<fs::PseudoDir>& svc_dir);
  zx_status_t InitCoreDevices(const char* sys_device_driver);
  bool InSuspend() const;
  bool InResume() const;

  zx_status_t ScanSystemDrivers();
  void BindDrivers();
  void UseFallbackDrivers();
  void DriverAdded(Driver* drv, const char* version);
  void DriverAddedInit(Driver* drv, const char* version);
  zx_status_t LibnameToVmo(const fbl::String& libname, zx::vmo* out_vmo) const;
  const Driver* LibnameToDriver(const fbl::StringPiece& libname) const;

  // Function that is invoked to request a driver try to bind to a device
  using AttemptBindFunc =
      fit::function<zx_status_t(const Driver* drv, const fbl::RefPtr<Device>& dev)>;

  // Attempts to bind the given driver to the given device.  Returns ZX_OK on
  // success, ZX_ERR_ALREADY_BOUND if there is a driver bound to the device
  // and the device is not allowed to be bound multiple times, ZX_ERR_NEXT if
  // the driver is not capable of binding to the device, and a different error
  // if the driver was capable of binding but failed to bind.
  zx_status_t BindDriverToDevice(const fbl::RefPtr<Device>& dev, const Driver* drv, bool autobind) {
    return BindDriverToDevice(dev, drv, autobind,
                              fit::bind_member(this, &Coordinator::AttemptBind));
  }

  // The same as above, but the given function is called to perform the
  // bind attempt.
  zx_status_t BindDriverToDevice(const fbl::RefPtr<Device>& dev, const Driver* drv, bool autobind,
                                 const AttemptBindFunc& attempt_bind);

  // Used to implement fuchsia::device::manager::Coordinator.
  // TODO(fxb/43370): remove |always_init| once init tasks can be enabled for all devices.
  zx_status_t AddDevice(const fbl::RefPtr<Device>& parent, zx::channel device_controller,
                        zx::channel coordinator,
                        const llcpp::fuchsia::device::manager::DeviceProperty* props_data,
                        size_t props_count, fbl::StringPiece name, uint32_t protocol_id,
                        fbl::StringPiece driver_path, fbl::StringPiece args, bool invisible,
                        bool has_init, bool always_init, zx::channel client_remote,
                        fbl::RefPtr<Device>* new_device);
  // Begin scheduling for removal of the device and unbinding of its children.
  void ScheduleRemove(const fbl::RefPtr<Device>& dev);
  // This is for scheduling the initial unbind task as a result of a devhost's |ScheduleRemove|
  // request.
  // If |do_unbind| is true, unbinding is also requested for |dev|.
  void ScheduleDevhostRequestedRemove(const fbl::RefPtr<Device>& dev, bool do_unbind = false);
  void ScheduleDevhostRequestedUnbindChildren(const fbl::RefPtr<Device>& parent);
  zx_status_t RemoveDevice(const fbl::RefPtr<Device>& dev, bool forced);
  zx_status_t MakeVisible(const fbl::RefPtr<Device>& dev);
  // Try binding a driver to the device. Returns ZX_ERR_ALREADY_BOUND if there
  // is a driver bound to the device and the device is not allowed to be bound multiple times.
  zx_status_t BindDevice(const fbl::RefPtr<Device>& dev, fbl::StringPiece drvlibname,
                         bool new_device);
  zx_status_t GetTopologicalPath(const fbl::RefPtr<const Device>& dev, char* out, size_t max) const;
  zx_status_t LoadFirmware(const fbl::RefPtr<Device>& dev, const char* path, zx::vmo* vmo,
                           size_t* size);

  zx_status_t GetMetadata(const fbl::RefPtr<Device>& dev, uint32_t type, void* buffer,
                          size_t buflen, size_t* size);
  zx_status_t GetMetadataSize(const fbl::RefPtr<Device>& dev, uint32_t type, size_t* size) {
    return GetMetadata(dev, type, nullptr, 0, size);
  }
  zx_status_t AddMetadata(const fbl::RefPtr<Device>& dev, uint32_t type, const void* data,
                          uint32_t length);
  zx_status_t PublishMetadata(const fbl::RefPtr<Device>& dev, const char* path, uint32_t type,
                              const void* data, uint32_t length);
  zx_status_t AddCompositeDevice(
      const fbl::RefPtr<Device>& dev, fbl::StringPiece name,
      llcpp::fuchsia::device::manager::CompositeDeviceDescriptor comp_desc);

  void DmMexec(zx::vmo kernel, zx::vmo bootdata);

  void HandleNewDevice(const fbl::RefPtr<Device>& dev);
  zx_status_t PrepareProxy(const fbl::RefPtr<Device>& dev, Devhost* target_devhost);

  void DumpState(VmoWriter* vmo) const;

  const zx::resource& root_resource() const { return config_.root_resource; }
  async_dispatcher_t* dispatcher() const { return config_.dispatcher; }
  llcpp::fuchsia::boot::Arguments::SyncClient* boot_args() const { return config_.boot_args; }
  bool disable_netsvc() const { return config_.disable_netsvc; }
  bool require_system() const { return config_.require_system; }
  bool suspend_fallback() const { return config_.suspend_fallback; }

  void set_running(bool running) { running_ = running; }
  bool system_available() const { return system_available_; }
  void set_system_available(bool system_available) { system_available_ = system_available; }
  bool system_loaded() const { return system_loaded_; }

  void set_loader_service_connector(LoaderServiceConnector loader_service_connector) {
    loader_service_connector_ = std::move(loader_service_connector);
  }

  fbl::DoublyLinkedList<Driver*, Driver::Node>& drivers() { return drivers_; }
  const fbl::DoublyLinkedList<Driver*, Driver::Node>& drivers() const { return drivers_; }
  fbl::DoublyLinkedList<fbl::RefPtr<Device>, Device::AllDevicesNode>& devices() { return devices_; }
  const fbl::DoublyLinkedList<fbl::RefPtr<Device>, Device::AllDevicesNode>& devices() const {
    return devices_;
  }

  void AppendPublishedMetadata(std::unique_ptr<Metadata> metadata) {
    published_metadata_.push_back(std::move(metadata));
  }

  const fbl::RefPtr<Device>& root_device() { return root_device_; }
  const fbl::RefPtr<Device>& misc_device() { return misc_device_; }
  const fbl::RefPtr<Device>& sys_device() { return sys_device_; }
  const fbl::RefPtr<Device>& test_device() { return test_device_; }

  void Suspend(uint32_t flags);
  void Suspend(SuspendContext ctx, fit::function<void(zx_status_t)> callback);

  void Resume(
      SystemPowerState target_state, ResumeCallback callback = [](zx_status_t) {});

  SuspendContext& suspend_context() { return suspend_context_; }
  const SuspendContext& suspend_context() const { return suspend_context_; }

  ResumeContext& resume_context() { return resume_context_; }
  const ResumeContext& resume_context() const { return resume_context_; }

  zx_status_t BindFidlServiceProxy(zx::channel listen_on);

  const Driver* fragment_driver() const { return fragment_driver_; }

  void ReleaseDevhost(Devhost* dh);

  InspectManager& inspect_manager() { return inspect_manager_; }

  // This method is public only for the test suite.
  zx_status_t BindDriver(Driver* drv, const AttemptBindFunc& attempt_bind);

 protected:
  std::unique_ptr<llcpp::fuchsia::fshost::Admin::SyncClient> fshost_admin_client_;

 private:
  CoordinatorConfig config_;
  bool running_ = false;
  bool launched_first_devhost_ = false;
  bool system_available_ = false;
  bool system_loaded_ = false;
  LoaderServiceConnector loader_service_connector_;

  // All Drivers
  fbl::DoublyLinkedList<Driver*, Driver::Node> drivers_;

  // Drivers to try last
  fbl::DoublyLinkedList<Driver*, Driver::Node> fallback_drivers_;

  // List of drivers loaded from /system by system_driver_loader()
  fbl::DoublyLinkedList<Driver*, Driver::Node> system_drivers_;

  // All Devices (excluding static immortal devices)
  fbl::DoublyLinkedList<fbl::RefPtr<Device>, Device::AllDevicesNode> devices_;

  // All DevHosts
  fbl::DoublyLinkedList<Devhost*> devhosts_;

  // All composite devices
  fbl::DoublyLinkedList<std::unique_ptr<CompositeDevice>, CompositeDevice::Node> composite_devices_;

  fbl::RefPtr<Device> root_device_;
  fbl::RefPtr<Device> misc_device_;
  fbl::RefPtr<Device> sys_device_;
  fbl::RefPtr<Device> test_device_;

  SuspendContext suspend_context_ = {};
  ResumeContext resume_context_;

  InspectManager inspect_manager_;

  // Power state control interface
  void Suspend(
      llcpp::fuchsia::hardware::power::statecontrol::SystemPowerState state,
      llcpp::fuchsia::hardware::power::statecontrol::Admin::Interface::SuspendCompleter::Sync
          completer) override;

  // Bind debugger interface
  void GetBindProgram(::fidl::StringView driver_path,
                      GetBindProgramCompleter::Sync completer) override;
  void GetDeviceProperties(::fidl::StringView name,
                           GetDevicePropertiesCompleter::Sync completer) override;

  void OnOOMEvent(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                  const zx_packet_signal_t* signal);
  async::WaitMethod<Coordinator, &Coordinator::OnOOMEvent> wait_on_oom_event_{this};

  fbl::DoublyLinkedList<std::unique_ptr<Metadata>, Metadata::Node> published_metadata_;

  // Once the special fragment driver is loaded, this will refer to it.  This
  // driver is used for binding against fragments of composite devices
  const Driver* fragment_driver_ = nullptr;

  void DumpDevice(VmoWriter* vmo, const Device* dev, size_t indent) const;
  void DumpDeviceProps(VmoWriter* vmo, const Device* dev) const;
  void DumpGlobalDeviceProps(VmoWriter* vmo) const;
  void DumpDrivers(VmoWriter* vmo) const;

  void BuildSuspendList();
  void Resume(ResumeContext ctx, std::function<void(zx_status_t)> callback);
  uint32_t GetSuspendFlagsFromSystemPowerState(
      llcpp::fuchsia::hardware::power::statecontrol::SystemPowerState state);

  std::unique_ptr<Driver> ValidateDriver(std::unique_ptr<Driver> drv);

  zx_status_t NewDevhost(const char* name, Devhost** out);

  zx_status_t BindDriver(Driver* drv) {
    return BindDriver(drv, fit::bind_member(this, &Coordinator::AttemptBind));
  }
  zx_status_t AttemptBind(const Driver* drv, const fbl::RefPtr<Device>& dev);
  void BindSystemDrivers();
  void DriverAddedSys(Driver* drv, const char* version);

  zx_status_t GetMetadataRecurse(const fbl::RefPtr<Device>& dev, uint32_t type, void* buffer,
                                 size_t buflen, size_t* size);

  // Shut down all filesystems (and fshost itself) by calling
  // fuchsia.fshost.Admin.Shutdown(). Note that this is called from multiple
  // different locations; during suspension, and in a low-memory situation.
  // Currently, both of these calls happen on the same dispatcher thread, but
  // consider thread safety when refactoring.
  void ShutdownFilesystems();
};

bool driver_is_bindable(const Driver* drv, uint32_t protocol_id,
                        const fbl::Array<const zx_device_prop_t>& props, bool autobind);

// Path to driver that should be bound to fragments of composite devices
extern const char* kFragmentDriverPath;

zx_status_t fidl_DirectoryWatch(void* ctx, uint32_t mask, uint32_t options, zx_handle_t raw_watcher,
                                fidl_txn_t* txn);

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_COORDINATOR_H_
