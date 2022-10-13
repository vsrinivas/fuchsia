// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_H_

#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <fidl/fuchsia.driver.development/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/ddk/device.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>

#include <memory>
#include <utility>
#include <variant>

#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/string.h>
#include <fbl/vector.h>

#include "src/devices/bin/driver_manager/composite_device.h"
#include "src/devices/bin/driver_manager/devfs.h"
#include "src/devices/bin/driver_manager/device_v2.h"
#include "src/devices/bin/driver_manager/inspect.h"
#include "src/devices/bin/driver_manager/metadata.h"
#include "src/devices/bin/driver_manager/v1/init_task.h"
#include "src/devices/bin/driver_manager/v1/resume_task.h"
#include "src/devices/bin/driver_manager/v1/suspend_task.h"
#include "src/devices/bin/driver_manager/v1/unbind_task.h"
#include "src/devices/bin/driver_manager/v2/node.h"

namespace fio = fuchsia_io;

class Coordinator;
class DriverHost;

// clang-format off

// This device is never destroyed
#define DEV_CTX_IMMORTAL           static_cast<uint32_t>(fuchsia_driver_development::DeviceFlags::kImmortal)

// This device requires that children are created in a
// new driver_host attached to a proxy device
#define DEV_CTX_MUST_ISOLATE           static_cast<uint32_t>(fuchsia_driver_development::DeviceFlags::kMustIsolate)

// This device may be bound multiple times
#define DEV_CTX_MULTI_BIND           static_cast<uint32_t>(fuchsia_driver_development::DeviceFlags::kMultiBind)

// This device is bound and not eligible for binding
// again until unbound.  Not allowed on MULTI_BIND ctx.
#define DEV_CTX_BOUND           static_cast<uint32_t>(fuchsia_driver_development::DeviceFlags::kBound)

// Device has been remove()'d
#define DEV_CTX_DEAD           static_cast<uint32_t>(fuchsia_driver_development::DeviceFlags::kDead)

// This device is a fragment of a composite device and
// can be part of multiple composite devices.
#define DEV_CTX_ALLOW_MULTI_COMPOSITE           static_cast<uint32_t>(fuchsia_driver_development::DeviceFlags::kAllowMultiComposite)

// Device is a proxy -- its "parent" is the device it's
// a proxy to.
#define DEV_CTX_PROXY           static_cast<uint32_t>(fuchsia_driver_development::DeviceFlags::kProxy)

// Device is not visible in devfs or bindable.
// Devices may be created in this state, but may not
// return to this state once made visible.
#define DEV_CTX_INVISIBLE           static_cast<uint32_t>(fuchsia_driver_development::DeviceFlags::kInvisible)

// Device should not go through auto-bind process
#define DEV_CTX_SKIP_AUTOBIND           static_cast<uint32_t>(fuchsia_driver_development::DeviceFlags::kSkipAutobind)

// Device is a bus device.
#define DEV_CTX_BUS_DEVICE           static_cast<uint32_t>(fuchsia_driver_development::DeviceFlags::kBusDevice)

// clang-format on

// Tags used for container membership identification
namespace internal {
struct DeviceChildListTag {};
struct DeviceDriverHostListTag {};
struct DeviceAllDevicesListTag {};
}  // namespace internal

class Device final
    : public dfv2::NodeManager,
      public fbl::RefCounted<Device>,
      public fidl::WireServer<fuchsia_device_manager::Coordinator>,
      public fbl::ContainableBaseClasses<
          fbl::TaggedDoublyLinkedListable<Device*, internal::DeviceChildListTag>,
          fbl::TaggedDoublyLinkedListable<Device*, internal::DeviceDriverHostListTag>,
          fbl::TaggedDoublyLinkedListable<fbl::RefPtr<Device>, internal::DeviceAllDevicesListTag>> {
 public:
  using ChildListTag = internal::DeviceChildListTag;
  using DriverHostListTag = internal::DeviceDriverHostListTag;
  using AllDevicesListTag = internal::DeviceAllDevicesListTag;

  Device(Coordinator* coord, fbl::String name, fbl::String libname, fbl::String args,
         fbl::RefPtr<Device> parent, uint32_t protocol_id, zx::vmo inspect,
         fidl::ClientEnd<fio::Directory> outgoing_dir);
  ~Device() override;

  // Create a new device with the given parameters.  This sets up its
  // relationship with its parent and driver_host and adds its RPC channel to the
  // coordinator's async loop.  This does not add the device to the
  // coordinator's devices_ list, or trigger publishing
  static zx_status_t Create(
      Coordinator* coordinator, const fbl::RefPtr<Device>& parent, fbl::String name,
      fbl::String driver_path, fbl::String args, uint32_t protocol_id,
      fbl::Array<zx_device_prop_t> props, fbl::Array<StrProperty> str_props,
      fidl::ServerEnd<fuchsia_device_manager::Coordinator> coordinator_request,
      fidl::ClientEnd<fuchsia_device_manager::DeviceController> device_controller,
      bool want_init_task, bool skip_autobind, zx::vmo inspect,
      fidl::ClientEnd<fio::Directory> outgoing_dir, fbl::RefPtr<Device>* device);

  // Create a new composite device.
  static zx_status_t CreateComposite(
      Coordinator* coordinator, fbl::RefPtr<DriverHost> driver_host, CompositeDevice& composite,
      fidl::ServerEnd<fuchsia_device_manager::Coordinator> coordinator_request,
      fidl::ClientEnd<fuchsia_device_manager::DeviceController> device_controller,
      fbl::RefPtr<Device>* device);
  zx_status_t CreateProxy();
  zx_status_t CreateNewProxy(fbl::RefPtr<Device>* new_proxy_out);

  void Serve(fidl::ServerEnd<fuchsia_device_manager::Coordinator> request);

  std::list<const Device*> children() const;
  std::list<Device*> children();

  // Signal that this device is ready for bind to happen.  This should happen
  // either immediately after the device is created, if it's created visible,
  // or after it becomes visible.
  zx_status_t SignalReadyForBind(zx::duration delay = zx::sec(0));

  using InitCompletion = fit::callback<void(zx_status_t)>;
  // Issue an Init request to this device.  When the response comes in, the
  // given completion will be invoked.
  void SendInit(InitCompletion completion);

  using SuspendCompletion = fit::callback<void(zx_status_t)>;
  // Issue a Suspend request to this device.  When the response comes in, the
  // given completion will be invoked.
  void SendSuspend(uint32_t flags, SuspendCompletion completion);

  using ResumeCompletion = fit::callback<void(zx_status_t)>;
  // Issue a Resume request to this device.  When the response comes in, the
  // given completion will be invoked.
  void SendResume(uint32_t target_system_state, ResumeCompletion completion);

  using UnbindCompletion = fit::callback<void(zx_status_t)>;
  using RemoveCompletion = fit::callback<void(zx_status_t)>;
  // Issue an Unbind request to this device, which will run the unbind hook.
  // When the response comes in, the given completion will be invoked.
  // If successful, returns ZX_OK and takes ownership of |completion|.
  void SendUnbind(UnbindCompletion& completion);
  // Issue a CompleteRemove request to this device.
  // When the response comes in, the given completion will be invoked.
  // If successful, returns ZX_OK and takes ownership of |completion|.
  void SendCompleteRemove(RemoveCompletion& completion);

  // Break the relationship between this device object and its parent
  void DetachFromParent();

  // Sets the properties of this device.
  zx_status_t SetProps(fbl::Array<const zx_device_prop_t> props);
  const fbl::Array<const zx_device_prop_t>& props() const { return props_; }

  const fbl::Array<const StrProperty>& str_props() const { return str_props_; }
  zx_status_t SetStrProps(fbl::Array<const StrProperty> str_props);

  const fbl::RefPtr<Device>& parent() { return parent_; }
  fbl::RefPtr<const Device> parent() const { return parent_; }

  const fbl::RefPtr<Device>& proxy() { return proxy_; }
  fbl::RefPtr<const Device> proxy() const { return proxy_; }

  const std::vector<fbl::RefPtr<Device>>& new_proxies() const { return new_proxies_; }

  uint32_t protocol_id() const { return protocol_id_; }

  DeviceInspect& inspect() { return inspect_; }

  bool is_bindable() const {
    return !(flags & (DEV_CTX_BOUND | DEV_CTX_INVISIBLE)) && (state_ != Device::State::kDead);
  }

  bool should_skip_autobind() const { return flags & DEV_CTX_SKIP_AUTOBIND; }

  bool is_visible() const { return !(flags & DEV_CTX_INVISIBLE); }

  bool is_composite_bindable() const {
    if (flags & (DEV_CTX_DEAD | DEV_CTX_INVISIBLE | DEV_CTX_SKIP_AUTOBIND)) {
      return false;
    }
    if ((flags & DEV_CTX_BOUND) && !(flags & DEV_CTX_ALLOW_MULTI_COMPOSITE)) {
      return false;
    }
    return true;
  }

  void push_fragment(CompositeDeviceFragment* fragment) { fragments_.push_back(fragment); }
  bool is_fragments_empty() { return fragments_.is_empty(); }

  fbl::TaggedDoublyLinkedList<CompositeDeviceFragment*, CompositeDeviceFragment::DeviceListTag>&
  fragments() {
    return fragments_;
  }
  std::optional<std::reference_wrapper<CompositeDevice>> composite() { return composite_; }
  std::optional<std::reference_wrapper<const CompositeDevice>> composite() const {
    return composite_;
  }
  bool is_composite() const { return composite_.has_value(); }
  void disassociate_from_composite() { composite_.reset(); }

  void set_host(fbl::RefPtr<DriverHost> host);
  const fbl::RefPtr<DriverHost>& host() const { return host_; }
  fbl::RefPtr<DriverHost>& host() { return host_; }

  void set_local_id(uint64_t local_id) {
    local_id_ = local_id;
    inspect().set_local_id(local_id);
  }
  uint64_t local_id() const { return local_id_; }

  const fbl::DoublyLinkedList<std::unique_ptr<Metadata>>& metadata() const { return metadata_; }
  void AddMetadata(std::unique_ptr<Metadata> md) { metadata_.push_front(std::move(md)); }

  // Creates the init task for the device.
  void CreateInitTask();
  // Returns the in-progress init task if it exists, nullptr otherwise.
  fbl::RefPtr<InitTask> GetActiveInit() { return active_init_; }

  // Run the completion for the outstanding init, if any.
  zx_status_t CompleteInit(zx_status_t status);

  // Returns the in-progress suspend task if it exists, nullptr otherwise.
  fbl::RefPtr<SuspendTask> GetActiveSuspend() { return active_suspend_; }
  // Creates a new suspend task if necessary and returns a reference to it.
  // If one is already in-progress, a reference to it is returned instead
  fbl::RefPtr<SuspendTask> RequestSuspendTask(uint32_t suspend_flags);

  fbl::RefPtr<ResumeTask> GetActiveResume() { return active_resume_; }
  void SetActiveResume(fbl::RefPtr<ResumeTask> resume_task) {
    active_resume_ = std::move(resume_task);
  }

  // Request Resume task
  fbl::RefPtr<ResumeTask> RequestResumeTask(uint32_t target_system_state);

  // Run the completion for the outstanding suspend, if any.  This method is
  // only exposed currently because RemoveDevice is on Coordinator instead of
  // Device.
  void CompleteSuspend(zx_status_t status);

  // Run the completion for the outstanding resume, if any.
  void CompleteResume(zx_status_t status);

  // Creates the unbind and remove tasks for the device if they do not already exist.
  // |opts| is used to configure the unbind task.
  void CreateUnbindRemoveTasks(UnbindTaskOpts opts);

  // Returns the in-progress unbind task if it exists, nullptr otherwise.
  // Unbind tasks are used to facilitate |Unbind| requests.
  fbl::RefPtr<UnbindTask> GetActiveUnbind() { return active_unbind_; }
  // Returns the in-progress remove task if it exists, nullptr otherwise.
  // Remove tasks are used to facilitate |CompleteRemove| requests.
  fbl::RefPtr<RemoveTask> GetActiveRemove() { return active_remove_; }

  // Run the completion for the outstanding unbind, if any.
  zx_status_t CompleteUnbind(zx_status_t status = ZX_OK);
  // Run the completion for the outstanding remove, if any.
  zx_status_t CompleteRemove(zx_status_t status = ZX_OK);

  // Drops the reference to the task.
  // This should be called if the device will not send an init, suspend, unbind or remove request.
  void DropInitTask() { active_init_ = nullptr; }
  void DropSuspendTask() { active_suspend_ = nullptr; }
  void DropUnbindTask() { active_unbind_ = nullptr; }
  void DropRemoveTask() { active_remove_ = nullptr; }

  bool has_outgoing_directory() { return outgoing_dir_.is_valid(); }
  fidl::ClientEnd<fio::Directory> take_outgoing_dir() { return std::move(outgoing_dir_); }
  fidl::ClientEnd<fio::Directory> clone_outgoing_dir() {
    return fidl::ClientEnd<fio::Directory>(
        zx::channel(fdio_service_clone(outgoing_dir_.handle()->get())));
  }

  const fbl::String& name() const { return name_; }
  const fbl::String& libname() const { return libname_; }
  const fbl::String& args() const { return args_; }

  Coordinator* coordinator;
  uint32_t flags = 0;

  // The backoff between each driver retry. This grows exponentially.
  zx::duration backoff = zx::msec(250);
  // The number of retries left for the driver.
  uint32_t retries = 4;

  std::optional<Devnode> self;
  // TODO(https://fxbug.dev/111253): These link nodes are currently always empty directories. Change
  // this to a pure `RemoteNode` that doesn't expose a directory.
  std::optional<Devnode> link;

  const fbl::String& link_name() const { return link_name_; }
  void set_link_name(fbl::String link_name) { link_name_ = std::move(link_name); }

  std::shared_ptr<dfv2::Node> GetBoundNode();
  zx::status<std::shared_ptr<dfv2::Node>> CreateDFv2Device();

  enum class State {
    kActive,
    /* The driver_host is in the process of running the device init hook.*/
    kInitializing,
    /* The driver_host is in the process of suspending the device.*/
    kSuspending,
    kSuspended,
    /* The driver_host is in the process of resuming the device.*/
    kResuming,
    /* Resume is complete. Will be marked active, after all children resume.*/
    kResumed,
    /* The driver_host is in the process of unbinding and removing the device.*/
    kUnbinding,
    /* The device has been remove()'d*/
    kDead,
  };

  void set_state(Device::State state);
  State state() const { return state_; }

  void inc_num_removal_attempts() { num_removal_attempts_++; }
  size_t num_removal_attempts() const { return num_removal_attempts_; }

  void InitializeInspectValues();

  void clear_active_resume() { active_resume_ = nullptr; }

  const fidl::WireSharedClient<fuchsia_device_manager::DeviceController>& device_controller()
      const {
    return device_controller_;
  }

  fidl::WireSharedClient<fuchsia_device_manager::DeviceController>& device_controller() {
    return device_controller_;
  }

  const std::optional<fidl::ServerBindingRef<fuchsia_device_manager::Coordinator>>&
  coordinator_binding() const {
    return coordinator_binding_;
  }

  fidl::ServerEnd<fuchsia_device_manager::DeviceController> ConnectDeviceController(
      async_dispatcher_t* dispatcher) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
    device_controller_.Bind(std::move(endpoints->client), dispatcher);
    return std::move(endpoints->server);
  }

  bool DriverLivesInSystemStorage() const;

  // Returns true if this device already has a driver bound.
  bool IsAlreadyBound() const;

  void UnpublishDevfs();

 private:
  // fuchsia_device_manager::Coordinator methods.
  void AddDevice(AddDeviceRequestView request, AddDeviceCompleter::Sync& _completer) override;
  void ScheduleRemove(ScheduleRemoveRequestView request,
                      ScheduleRemoveCompleter::Sync& _completer) override;
  void AddCompositeDevice(AddCompositeDeviceRequestView request,
                          AddCompositeDeviceCompleter::Sync& _completer) override;
  void AddDeviceGroup(AddDeviceGroupRequestView request,
                      AddDeviceGroupCompleter::Sync& _completer) override;
  void BindDevice(BindDeviceRequestView request, BindDeviceCompleter::Sync& _completer) override;
  void GetTopologicalPath(GetTopologicalPathCompleter::Sync& _completer) override;
  void LoadFirmware(LoadFirmwareRequestView request,
                    LoadFirmwareCompleter::Sync& _completer) override;
  void GetMetadata(GetMetadataRequestView request, GetMetadataCompleter::Sync& _completer) override;
  void GetMetadataSize(GetMetadataSizeRequestView request,
                       GetMetadataSizeCompleter::Sync& _completer) override;
  void AddMetadata(AddMetadataRequestView request, AddMetadataCompleter::Sync& _completer) override;
  void ScheduleUnbindChildren(ScheduleUnbindChildrenCompleter::Sync& _completer) override;

  // dfv2::NodeManager
  void Bind(dfv2::Node& node, std::shared_ptr<dfv2::BindResultTracker> result_tracker) override {}

  zx::status<dfv2::DriverHost*> CreateDriverHost() override {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // This is a template so we can share the same code between a nonconst and
  // const version.
  template <typename DeviceType>
  static std::list<DeviceType*> GetChildren(DeviceType* device);

  fidl::WireSharedClient<fuchsia_device_manager::DeviceController> device_controller_;
  std::optional<fidl::ServerBindingRef<fuchsia_device_manager::Coordinator>> coordinator_binding_;

  const fbl::String name_;
  const fbl::String libname_;
  const fbl::String args_;

  // The device's parent in the device tree. If this is a composite device, its
  // parent will be null.
  fbl::RefPtr<Device> parent_;
  const uint32_t protocol_id_;

  fbl::RefPtr<Device> proxy_;
  // A vector of proxies to allow the device's children to connect to its
  // outgoing directory. Each new proxy has its connection to the outgoing
  // directory.
  std::vector<fbl::RefPtr<Device>> new_proxies_;

  fbl::Array<const zx_device_prop_t> props_;

  fbl::Array<const StrProperty> str_props_;

  async::TaskClosure publish_task_;

  // List of all child devices of this device.
  //
  // Does not include composite devices except when `this` is the composite device's primary
  // fragment. The primary fragment is used because the use of intrusive nodes precludes us from
  // modeling multiple parents.
  fbl::TaggedDoublyLinkedList<Device*, ChildListTag> children_;

  // Metadata entries associated to this device.
  fbl::DoublyLinkedList<std::unique_ptr<Metadata>> metadata_;

  // list of all fragments that this device bound to.
  fbl::TaggedDoublyLinkedList<CompositeDeviceFragment*, CompositeDeviceFragment::DeviceListTag>
      fragments_;

  std::optional<std::reference_wrapper<CompositeDevice>> composite_;

  fbl::RefPtr<DriverHost> host_;
  // The id of this device from the perspective of the driver_host.  This can be
  // used to communicate with the driver_host about this device.
  uint64_t local_id_ = 0;

  // The current state of the device
  State state_ = State::kActive;

  // If an init is in-progress, this task represents it.
  fbl::RefPtr<InitTask> active_init_;
  // If an init is in-progress, this completion will be invoked when it is
  // completed.  It will likely mark |active_init_| as completed and clear it.
  InitCompletion init_completion_;

  // If a suspend is in-progress, this task represents it.
  fbl::RefPtr<SuspendTask> active_suspend_;
  // If a suspend is in-progress, this completion will be invoked when it is
  // completed.  It will likely mark |active_suspend_| as completed and clear
  // it.
  SuspendCompletion suspend_completion_;

  // If a resume is in-progress, this task represents it.
  fbl::RefPtr<ResumeTask> active_resume_;
  // If a Resume is in-progress, this completion will be invoked when it is
  // completed.
  ResumeCompletion resume_completion_;

  // If an unbind is in-progress, this task represents it.
  fbl::RefPtr<UnbindTask> active_unbind_;
  // If an unbind is in-progress, this completion will be invoked when it is
  // completed. It will likely mark |active_unbind_| as completed and clear
  // it.
  UnbindCompletion unbind_completion_;

  // If a remove is in-progress, this task represents it.
  fbl::RefPtr<RemoveTask> active_remove_;
  // If a remove is in-progress, this completion will be invoked when it is
  // completed. It will likely mark |active_remove_| as completed and clear
  // it.
  RemoveCompletion remove_completion_;

  // Name of the inspect vmo file as it appears in diagnostics class directory
  fbl::String link_name_;

  // Provides incoming directory for the driver which binds to this device.
  fidl::ClientEnd<fio::Directory> outgoing_dir_;

  // This lets us check for unexpected removals and is for testing use only.
  size_t num_removal_attempts_ = 0;

  // This is the symbol we got from the driver host for the device's banjo protocol.
  uint64_t dfv2_device_symbol_ = 0;

  // If this is not null, there is a DFv2 driver bound to this device.
  std::shared_ptr<dfv2::Device> dfv2_bound_device_;

  DeviceInspect inspect_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_H_
