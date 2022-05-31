// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_H_

#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/ddk/device.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>

#include <memory>
#include <variant>

#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/string.h>

#include "src/devices/bin/driver_manager/composite_device.h"
#include "src/devices/bin/driver_manager/inspect.h"
#include "src/devices/bin/driver_manager/metadata.h"
#include "src/lib/storage/vfs/cpp/vmo_file.h"

namespace fio = fuchsia_io;

class Coordinator;
class DriverHost;
struct Devnode;
class InitTask;
class RemoveTask;
class SuspendTask;
class ResumeTask;
class UnbindTask;
struct UnbindTaskOpts;

// clang-format off

// This device is never destroyed
#define DEV_CTX_IMMORTAL           0x0001

// This device requires that children are created in a
// new driver_host attached to a proxy device
#define DEV_CTX_MUST_ISOLATE       0x0002

// This device may be bound multiple times
#define DEV_CTX_MULTI_BIND         0x0004

// This device is bound and not eligible for binding
// again until unbound.  Not allowed on MULTI_BIND ctx.
#define DEV_CTX_BOUND              0x0008

// Device has been remove()'d
#define DEV_CTX_DEAD               0x0010

// This device is a fragment of a composite device and
// can be part of multiple composite devices.
#define DEV_CTX_ALLOW_MULTI_COMPOSITE    0x0020

// Device is a proxy -- its "parent" is the device it's
// a proxy to.
#define DEV_CTX_PROXY              0x0040

// Device is not visible in devfs or bindable.
// Devices may be created in this state, but may not
// return to this state once made visible.
#define DEV_CTX_INVISIBLE          0x0080

// Device should not go through auto-bind process
#define DEV_CTX_SKIP_AUTOBIND      0x0100

// clang-format on

// Tags used for container membership identification
namespace internal {
struct DeviceChildListTag {};
struct DeviceDriverHostListTag {};
struct DeviceAllDevicesListTag {};
}  // namespace internal

class Device
    : public fbl::RefCounted<Device>,
      public fidl::WireServer<fuchsia_device_manager::Coordinator>,
      public fbl::ContainableBaseClasses<
          fbl::TaggedDoublyLinkedListable<Device*, internal::DeviceChildListTag>,
          fbl::TaggedDoublyLinkedListable<Device*, internal::DeviceDriverHostListTag>,
          fbl::TaggedDoublyLinkedListable<fbl::RefPtr<Device>, internal::DeviceAllDevicesListTag>> {
 public:
  using ChildListTag = internal::DeviceChildListTag;
  using DriverHostListTag = internal::DeviceDriverHostListTag;
  using AllDevicesListTag = internal::DeviceAllDevicesListTag;

  void AddDevice(AddDeviceRequestView request, AddDeviceCompleter::Sync& _completer) override;
  void ScheduleRemove(ScheduleRemoveRequestView request,
                      ScheduleRemoveCompleter::Sync& _completer) override;
  void AddCompositeDevice(AddCompositeDeviceRequestView request,
                          AddCompositeDeviceCompleter::Sync& _completer) override;
  void AddDeviceGroup(AddDeviceGroupRequestView request,
                      AddDeviceGroupCompleter::Sync& _completer) override;
  void BindDevice(BindDeviceRequestView request, BindDeviceCompleter::Sync& _completer) override;
  void GetTopologicalPath(GetTopologicalPathRequestView request,
                          GetTopologicalPathCompleter::Sync& _completer) override;
  void LoadFirmware(LoadFirmwareRequestView request,
                    LoadFirmwareCompleter::Sync& _completer) override;
  void GetMetadata(GetMetadataRequestView request, GetMetadataCompleter::Sync& _completer) override;
  void GetMetadataSize(GetMetadataSizeRequestView request,
                       GetMetadataSizeCompleter::Sync& _completer) override;
  void AddMetadata(AddMetadataRequestView request, AddMetadataCompleter::Sync& _completer) override;
  void ScheduleUnbindChildren(ScheduleUnbindChildrenRequestView request,
                              ScheduleUnbindChildrenCompleter::Sync& _completer) override;

  // This iterator provides access to a list of devices that does not provide
  // mechanisms for mutating that list.  With this, a user can get mutable
  // access to a device in the list.  This is achieved by making the linked
  // list iterator opaque. It is not safe to modify the underlying list while
  // this iterator is in use.
  template <typename ChildIterType, typename FragmentIterType, typename DeviceType>
  class ChildListIterator {
   public:
    ChildListIterator() : state_(Done{}) {}
    explicit ChildListIterator(DeviceType* device)
        : state_(device->children_.begin()), device_(device) {
      SkipInvalidStates();
    }
    ChildListIterator operator++(int) {
      auto other = *this;
      ++*this;
      return other;
    }
    bool operator==(const ChildListIterator& other) const { return state_ == other.state_; }
    bool operator!=(const ChildListIterator& other) const { return !(state_ == other.state_); }

    // The iterator implementation for the child list.  This is the source of truth
    // for what devices are children of the device.
    ChildListIterator& operator++() {
      std::visit(
          [this](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, ChildIterType> || std::is_same_v<T, FragmentIterType>) {
              ++arg;
            } else if constexpr (std::is_same_v<T, Done>) {
              state_ = Done{};
            }
          },
          state_);
      SkipInvalidStates();
      return *this;
    }

    DeviceType& operator*() const {
      return std::visit(
          [](auto&& arg) -> DeviceType& {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, ChildIterType>) {
              return *arg;
            } else if constexpr (std::is_same_v<T, FragmentIterType>) {
              return *(arg->composite()->device());
            } else {
              __builtin_trap();
            }
          },
          state_);
    }

   private:
    // Advance the iterator to the next valid state or reach the done state.
    // This is used to handle advancement between the different state variants.
    void SkipInvalidStates() {
      bool more = true;
      while (more) {
        more = std::visit(
            [this](auto&& arg) {
              using T = std::decay_t<decltype(arg)>;
              if constexpr (std::is_same_v<T, ChildIterType>) {
                // Check if there are any more children in the list.  If
                // there are, we're in a valid state and can stop.
                // Otherwise, advance to the next variant and check if
                // it's a valid state.
                if (arg != device_->children_.end()) {
                  return false;
                }

                // If there are no more children and this is a fragment device,
                // find children of the fragment device by looking at its parent's
                // fragment list.
                if (device_->libname() == device_->coordinator->GetFragmentDriverUrl()) {
                  if (device_->parent_) {
                    state_ = FragmentIterType{device_->parent()->fragments_.begin()};
                    return true;
                  }
                  state_ = Done{};
                  return false;
                }

                // Some composite devices are added directly as fragments without
                // a proxy fragment device. These don't appear in the children list.
                state_ = FragmentIterType{device_->fragments_.begin()};
                return true;
              } else if constexpr (std::is_same_v<T, FragmentIterType>) {
                if (device_->libname() == device_->coordinator->GetFragmentDriverUrl()) {
                  // This device is an internal fragment device.
                  if (arg == device_->parent()->fragments_.end()) {
                    state_ = Done{};
                    return false;
                  }

                  // Skip composite devices that aren't yet bound.
                  if (arg->composite()->device() == nullptr) {
                    state_ = FragmentIterType{++arg};
                    return true;
                  }

                  // Skip any fragments that aren't bound to this fragment device.
                  if (arg->fragment_device().get() != device_) {
                    state_ = FragmentIterType{++arg};
                    return true;
                  }

                  return false;
                }

                // Check for composite devices that don't have proxy fragment devices.

                if (arg == device_->fragments_.end()) {
                  state_ = Done{};
                  return false;
                }

                // Skip composite devices that aren't yet bound.
                if (arg->composite()->device() == nullptr) {
                  state_ = FragmentIterType{++arg};
                  return true;
                }

                // Skip fragments that have a fragment device.
                if (arg->fragment_device() != nullptr) {
                  state_ = FragmentIterType{++arg};
                  return true;
                }

                return false;
              } else if constexpr (std::is_same_v<T, Done>) {
                return false;
              }
            },
            state_);
      }
    }
    struct Done {
      bool operator==(Done) const { return true; }
    };
    std::variant<ChildIterType, FragmentIterType, Done> state_;
    DeviceType* device_;
  };

  // This class exists to allow consumers of the Device class to write
  //   for (auto& child : dev->children())
  // and get mutable access to the children without getting mutable access to
  // the list.
  template <typename DeviceType, typename IterType>
  class ChildListIteratorFactory {
   public:
    explicit ChildListIteratorFactory(DeviceType* device) : device_(device) {}

    IterType begin() const { return IterType(device_); }
    IterType end() const { return IterType(); }

    bool is_empty() const { return begin() == end(); }

   private:
    DeviceType* device_;
  };

  Device(Coordinator* coord, fbl::String name, fbl::String libname, fbl::String args,
         fbl::RefPtr<Device> parent, uint32_t protocol_id, zx::vmo inspect,
         zx::channel client_remote, fidl::ClientEnd<fio::Directory> outgoing_dir);
  ~Device();

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
      bool want_init_task, bool skip_autobind, zx::vmo inspect, zx::channel client_remote,
      fidl::ClientEnd<fio::Directory> outgoing_dir, fbl::RefPtr<Device>* device);
  static zx_status_t CreateComposite(
      Coordinator* coordinator, fbl::RefPtr<DriverHost> driver_host,
      const CompositeDevice& composite,
      fidl::ServerEnd<fuchsia_device_manager::Coordinator> coordinator_request,
      fidl::ClientEnd<fuchsia_device_manager::DeviceController> device_controller,
      fbl::RefPtr<Device>* device);
  zx_status_t CreateProxy();
  zx_status_t CreateNewProxy();

  static void Bind(fbl::RefPtr<Device> dev, async_dispatcher_t*,
                   fidl::ServerEnd<fuchsia_device_manager::Coordinator>);

  // We do not want to expose the list itself for mutation, even if the
  // children are allowed to be mutated.  We manage this by making the
  // iterator opaque.
  using NonConstChildListIterator = ChildListIterator<
      fbl::TaggedDoublyLinkedList<Device*, ChildListTag>::iterator,
      fbl::TaggedDoublyLinkedList<CompositeDeviceFragment*,
                                  CompositeDeviceFragment::DeviceListTag>::iterator,
      Device>;
  using ConstChildListIterator = ChildListIterator<
      fbl::TaggedDoublyLinkedList<Device*, ChildListTag>::const_iterator,
      fbl::TaggedDoublyLinkedList<CompositeDeviceFragment*,
                                  CompositeDeviceFragment::DeviceListTag>::const_iterator,
      const Device>;

  using NonConstFragmentListIterator =
      fbl::TaggedDoublyLinkedList<CompositeDeviceFragment*,
                                  CompositeDeviceFragment::DeviceListTag>::iterator;

  using NonConstChildListIteratorFactory =
      ChildListIteratorFactory<Device, NonConstChildListIterator>;
  using ConstChildListIteratorFactory =
      ChildListIteratorFactory<const Device, ConstChildListIterator>;
  NonConstChildListIteratorFactory children() { return NonConstChildListIteratorFactory(this); }
  ConstChildListIteratorFactory children() const { return ConstChildListIteratorFactory(this); }

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

  const fbl::RefPtr<Device>& new_proxy() { return new_proxy_; }
  fbl::RefPtr<const Device> new_proxy() const { return new_proxy_; }

  uint32_t protocol_id() const { return protocol_id_; }

  DeviceInspect& inspect() { return *inspect_; }

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
  // If the device was created as a composite, this returns its description.
  CompositeDevice* composite() const {
    auto val = std::get_if<CompositeDevice*>(&composite_);
    return val ? *val : nullptr;
  }
  void set_composite(CompositeDevice* composite) {
    ZX_ASSERT(std::holds_alternative<UnassociatedWithComposite>(composite_));
    composite_ = composite;
  }
  bool is_composite() const { return composite() != nullptr; }
  void disassociate_from_composite() { composite_ = UnassociatedWithComposite{}; }

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
  void SetActiveResume(fbl::RefPtr<ResumeTask> resume_task) { active_resume_ = resume_task; }

  // Request Resume task
  fbl::RefPtr<ResumeTask> RequestResumeTask(uint32_t system_resume_state);

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

  zx::channel take_client_remote() { return std::move(client_remote_); }
  bool has_outgoing_directory() { return outgoing_dir_.is_valid(); }
  fidl::ClientEnd<fio::Directory> take_outgoing_dir() { return std::move(outgoing_dir_); }

  const fbl::String& name() const { return name_; }
  const fbl::String& libname() const { return libname_; }
  const fbl::String& args() const { return args_; }

  Coordinator* coordinator;
  uint32_t flags = 0;

  // The backoff between each driver retry. This grows exponentially.
  zx::duration backoff = zx::msec(250);
  // The number of retries left for the driver.
  uint32_t retries = 4;
  Devnode* self = nullptr;
  Devnode* link = nullptr;

  Devnode* devnode() { return self; }

  const fbl::String& link_name() const { return link_name_; }
  void set_link_name(fbl::String link_name) { link_name_ = std::move(link_name); }

  fbl::RefPtr<fs::VmoFile>& inspect_file() { return inspect_file_; }

  // TODO(teisenbe): We probably want more states.
#define STATE_VALUES(macro)                                                                        \
  macro(kActive)                                                                                   \
      macro(kInitializing) /* The driver_host is in the process of running the device init hook.*/ \
      macro(kSuspending)   /* The driver_host is in the process of suspending the device.*/        \
      macro(kSuspended)                                                                            \
          macro(kResuming) /* The driver_host is in the process of resuming the device.*/          \
      macro(kResumed) /* Resume is complete. Will be marked active, after all children resume.*/   \
      macro(                                                                                       \
          kUnbinding) /* The driver_host is in the process of unbinding and removing the device.*/ \
      macro(kDead)    /* The device has been remove()'d*/

#define MAKE_ENUM_VALUE(state) state,
  enum class State { STATE_VALUES(MAKE_ENUM_VALUE) };
#undef ENUM_VALUE
#define MAKE_SWITCH_STATEMENT(state) \
  case State::state:                 \
    return #state;
  static std::string StateToString(State state) {
    switch (state) { STATE_VALUES(MAKE_SWITCH_STATEMENT) }
  }
#undef MAKE_SWITCH_STATEMENT
#undef STATE_VALUES

  void set_state(Device::State state) {
    state_ = state;
    inspect().set_state(StateToString(state));

    if (state == Device::State::kDead) {
      if (std::optional binding = std::exchange(coordinator_binding_, std::nullopt);
          binding.has_value()) {
        binding.value().Unbind();
      }
    }
  }

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

  const fidl::ServerEnd<fuchsia_device_manager::DeviceController> ConnectDeviceController(
      async_dispatcher_t* dispatcher) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
    device_controller_.Bind(std::move(endpoints->client), dispatcher);
    return std::move(endpoints->server);
  }

  bool DriverLivesInSystemStorage() const;

  // Returns true if this device already has a driver bound.
  bool IsAlreadyBound() const;

 private:
  fidl::WireSharedClient<fuchsia_device_manager::DeviceController> device_controller_;
  std::optional<fidl::ServerBindingRef<fuchsia_device_manager::Coordinator>> coordinator_binding_;

  const fbl::String name_;
  const fbl::String libname_;
  const fbl::String args_;

  fbl::RefPtr<Device> parent_;
  const uint32_t protocol_id_;

  fbl::RefPtr<Device> proxy_;
  fbl::RefPtr<Device> new_proxy_;

  fbl::Array<const zx_device_prop_t> props_;

  fbl::Array<const StrProperty> str_props_;

  async::TaskClosure publish_task_;

  // List of all child devices of this device, except for composite devices.
  // Composite devices are excluded because their multiple-parent nature
  // precludes using the same intrusive nodes as single-parent devices.
  fbl::TaggedDoublyLinkedList<Device*, ChildListTag> children_;

  // Metadata entries associated to this device.
  fbl::DoublyLinkedList<std::unique_ptr<Metadata>> metadata_;

  // list of all fragments that this device bound to.
  fbl::TaggedDoublyLinkedList<CompositeDeviceFragment*, CompositeDeviceFragment::DeviceListTag>
      fragments_;

  // - If this device is part of a composite device, this is inhabited by
  //   CompositeDeviceFragment* and it points to the fragment that matched it.
  //   Note that this is only set on the device that matched the fragment, not
  //   the "fragment device" added by the fragment driver.
  // - If this device is a composite device, this is inhabited by
  //   CompositeDevice* and it points to the composite that describes it.
  // - Otherwise, it is inhabited by UnassociatedWithComposite
  struct UnassociatedWithComposite {};
  std::variant<UnassociatedWithComposite, CompositeDevice*> composite_;

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

  fbl::RefPtr<fs::VmoFile> inspect_file_;

  // For attaching as an open connection to the proxy device,
  // or once the device becomes visible.
  zx::channel client_remote_;

  // Provides incoming directory for the driver which binds to this device.
  fidl::ClientEnd<fio::Directory> outgoing_dir_;

  // This lets us check for unexpected removals and is for testing use only.
  size_t num_removal_attempts_ = 0;

  std::optional<DeviceInspect> inspect_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_H_
