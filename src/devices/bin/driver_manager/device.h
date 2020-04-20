// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_H_

#include <fuchsia/device/manager/c/fidl.h>
#include <fuchsia/device/manager/cpp/fidl.h>
#include <fuchsia/device/manager/llcpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>

#include <memory>
#include <variant>

#include <ddk/device.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/string.h>

#include "async_loop_ref_counted_rpc_handler.h"
#include "composite_device.h"
#include "driver_test_reporter.h"
#include "metadata.h"

class Coordinator;
class Devhost;
struct Devnode;
class InitTask;
class RemoveTask;
class SuspendContext;
class SuspendTask;
class ResumeTask;
class UnbindTask;
struct UnbindTaskOpts;

// clang-format off

// This device is never destroyed
#define DEV_CTX_IMMORTAL           0x01

// This device requires that children are created in a
// new devhost attached to a proxy device
#define DEV_CTX_MUST_ISOLATE       0x02

// This device may be bound multiple times
#define DEV_CTX_MULTI_BIND         0x04

// This device is bound and not eligible for binding
// again until unbound.  Not allowed on MULTI_BIND ctx.
#define DEV_CTX_BOUND              0x08

// Device has been remove()'d
#define DEV_CTX_DEAD               0x10

// This device is a fragment of a composite device and
// can be part of multiple composite devices.
#define DEV_CTX_ALLOW_MULTI_COMPOSITE    0x20

// Device is a proxy -- its "parent" is the device it's
// a proxy to.
#define DEV_CTX_PROXY              0x40

// Device is not visible in devfs or bindable.
// Devices may be created in this state, but may not
// return to this state once made visible.
#define DEV_CTX_INVISIBLE          0x80

// Signals used on the test event
#define TEST_BIND_DONE_SIGNAL ZX_USER_SIGNAL_0
#define TEST_SUSPEND_DONE_SIGNAL ZX_USER_SIGNAL_1
#define TEST_RESUME_DONE_SIGNAL ZX_USER_SIGNAL_2
#define TEST_REMOVE_DONE_SIGNAL ZX_USER_SIGNAL_3

constexpr zx::duration kDefaultTestTimeout = zx::sec(5);

// clang-format on

class Device : public fbl::RefCounted<Device>,
               public llcpp::fuchsia::device::manager::Coordinator::Interface,
               public AsyncLoopRefCountedRpcHandler<Device> {
 public:
  void AddDevice(::zx::channel coordinator, ::zx::channel device_controller,
                 ::fidl::VectorView<llcpp::fuchsia::device::manager::DeviceProperty> props,
                 ::fidl::StringView name, uint32_t protocol_id, ::fidl::StringView driver_path,
                 ::fidl::StringView args,
                 llcpp::fuchsia::device::manager::AddDeviceConfig device_add_config, bool has_init,
                 ::zx::channel client_remote, AddDeviceCompleter::Sync _completer) override;
  void ScheduleRemove(bool unbind_self, ScheduleRemoveCompleter::Sync _completer) override;
  void AddCompositeDevice(::fidl::StringView name,
                          llcpp::fuchsia::device::manager::CompositeDeviceDescriptor comp_desc,
                          AddCompositeDeviceCompleter::Sync _completer) override;
  void PublishMetadata(::fidl::StringView device_path, uint32_t key,
                       ::fidl::VectorView<uint8_t> data,
                       PublishMetadataCompleter::Sync _completer) override;
  void AddDeviceInvisible(::zx::channel coordinator, ::zx::channel device_controller,
                          ::fidl::VectorView<llcpp::fuchsia::device::manager::DeviceProperty> props,
                          ::fidl::StringView name, uint32_t protocol_id,
                          ::fidl::StringView driver_path, ::fidl::StringView args, bool has_init,
                          ::zx::channel client_remote,
                          AddDeviceInvisibleCompleter::Sync _completer) override;
  void MakeVisible(MakeVisibleCompleter::Sync _completer) override;
  void BindDevice(::fidl::StringView driver_path, BindDeviceCompleter::Sync _completer) override;
  void GetTopologicalPath(GetTopologicalPathCompleter::Sync _completer) override;
  void LoadFirmware(::fidl::StringView fw_path, LoadFirmwareCompleter::Sync _completer) override;
  void GetMetadata(uint32_t key, GetMetadataCompleter::Sync _completer) override;
  void GetMetadataSize(uint32_t key, GetMetadataSizeCompleter::Sync _completer) override;
  void AddMetadata(uint32_t key, ::fidl::VectorView<uint8_t> data,
                   AddMetadataCompleter::Sync _completer) override;
  void ScheduleUnbindChildren(ScheduleUnbindChildrenCompleter::Sync _completer) override;
  void RunCompatibilityTests(int64_t hook_wait_time,
                             RunCompatibilityTestsCompleter::Sync _completer) override;
  void DirectoryWatch(uint32_t mask, uint32_t options, ::zx::channel watcher,
                      DirectoryWatchCompleter::Sync _completer) override;

  // Node for entry in device child list
  struct Node {
    static fbl::DoublyLinkedListNodeState<Device*>& node_state(Device& obj) { return obj.node_; }
  };

  struct DevhostNode {
    static fbl::DoublyLinkedListNodeState<Device*>& node_state(Device& obj) {
      return obj.devhost_node_;
    }
  };

  struct AllDevicesNode {
    static fbl::DoublyLinkedListNodeState<fbl::RefPtr<Device>>& node_state(Device& obj) {
      return obj.all_devices_node_;
    }
  };

  // This iterator provides access to a list of devices that does not provide
  // mechanisms for mutating that list.  With this, a user can get mutable
  // access to a device in the list.  This is achieved by making the linked
  // list iterator opaque. It is not safe to modify the underlying list while
  // this iterator is in use.
  template <typename IterType, typename DeviceType>
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
            if constexpr (std::is_same_v<T, IterType>) {
              ++arg;
            } else if constexpr (std::is_same_v<T, Composite>) {
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
            if constexpr (std::is_same_v<T, IterType>) {
              return *arg;
            } else if constexpr (std::is_same_v<T, Composite>) {
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
              if constexpr (std::is_same_v<T, IterType>) {
                // Check if there are any more children in the list.  If
                // there are, we're in a valid state and can stop.
                // Otherwise, advance to the next variant and check if
                // it's a valid state.
                if (arg != device_->children_.end()) {
                  return false;
                }
                // If there are no more children, run through the Composite
                // state next.
                if (device_->parent_) {
                  state_ = Composite{device_->parent_->fragments().begin()};
                } else {
                  state_ = Composite{};
                }
                return true;
              } else if constexpr (std::is_same_v<T, Composite>) {
                // Check if this device is an internal fragment device
                // that bound to a composite fragment.  If it is, and
                // the composite has been constructed, the iterator
                // should yield the composite.
                if (device_->parent_) {
                  if (arg != device_->parent_->fragments().end() &&
                      arg->composite()->device() != nullptr) {
                    return false;
                  }
                }
                state_ = Done{};
                return false;
              } else if constexpr (std::is_same_v<T, Done>) {
                return false;
              }
            },
            state_);
      }
    }

    using Composite = fbl::DoublyLinkedList<CompositeDeviceFragment*,
                                            CompositeDeviceFragment::DeviceNode>::iterator;
    struct Done {
      bool operator==(Done) const { return true; }
    };
    std::variant<IterType, Composite, Done> state_;
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
         fbl::RefPtr<Device> parent, uint32_t protocol_id, zx::channel client_remote,
         bool wait_make_visible = false);
  ~Device();

  // Create a new device with the given parameters.  This sets up its
  // relationship with its parent and devhost and adds its RPC channel to the
  // coordinator's async loop.  This does not add the device to the
  // coordinator's devices_ list, or trigger publishing
  static zx_status_t Create(Coordinator* coordinator, const fbl::RefPtr<Device>& parent,
                            fbl::String name, fbl::String driver_path, fbl::String args,
                            uint32_t protocol_id, fbl::Array<zx_device_prop_t> props,
                            zx::channel coordinator_rpc, zx::channel device_controller_rpc,
                            bool wait_make_visible, bool want_init_task, zx::channel client_remote,
                            fbl::RefPtr<Device>* device);
  static zx_status_t CreateComposite(Coordinator* coordinator, fbl::RefPtr<Devhost> devhost,
                                     const CompositeDevice& composite, zx::channel coordinator_rpc,
                                     zx::channel device_controller_rpc,
                                     fbl::RefPtr<Device>* device);
  zx_status_t CreateProxy();

  static void HandleRpc(fbl::RefPtr<Device>&& dev, async_dispatcher_t* dispatcher,
                        async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);

  // We do not want to expose the list itself for mutation, even if the
  // children are allowed to be mutated.  We manage this by making the
  // iterator opaque.
  using NonConstChildListIterator =
      ChildListIterator<fbl::DoublyLinkedList<Device*, Node>::iterator, Device>;
  using ConstChildListIterator =
      ChildListIterator<fbl::DoublyLinkedList<Device*, Node>::const_iterator, const Device>;
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
  zx_status_t SendInit(InitCompletion completion);

  using SuspendCompletion = fit::callback<void(zx_status_t)>;
  // Issue a Suspend request to this device.  When the response comes in, the
  // given completion will be invoked.
  zx_status_t SendSuspend(uint32_t flags, SuspendCompletion completion);

  using ResumeCompletion = fit::callback<void(zx_status_t)>;
  // Issue a Resume request to this device.  When the response comes in, the
  // given completion will be invoked.
  zx_status_t SendResume(uint32_t target_system_state, ResumeCompletion completion);

  using UnbindCompletion = fit::callback<void(zx_status_t)>;
  using RemoveCompletion = fit::callback<void(zx_status_t)>;
  // Issue an Unbind request to this device, which will run the unbind hook.
  // When the response comes in, the given completion will be invoked.
  zx_status_t SendUnbind(UnbindCompletion completion);
  // Issue a CompleteRemoval request to this device.
  // When the response comes in, the given completion will be invoked.
  zx_status_t SendCompleteRemoval(RemoveCompletion completion);

  // Break the relationship between this device object and its parent
  void DetachFromParent();

  // Sets the properties of this device.  Returns an error if the properties
  // array contains more than one property from the BIND_TOPO_* range.
  zx_status_t SetProps(fbl::Array<const zx_device_prop_t> props);
  const fbl::Array<const zx_device_prop_t>& props() const { return props_; }
  const zx_device_prop_t* topo_prop() const { return topo_prop_; }

  const fbl::RefPtr<Device>& parent() { return parent_; }
  fbl::RefPtr<const Device> parent() const { return parent_; }

  const fbl::RefPtr<Device>& proxy() { return proxy_; }
  fbl::RefPtr<const Device> proxy() const { return proxy_; }

  uint32_t protocol_id() const { return protocol_id_; }

  bool is_bindable() const {
    return !(flags & (DEV_CTX_BOUND | DEV_CTX_INVISIBLE)) && (state_ != Device::State::kDead);
  }

  bool is_visible() const { return !(flags & DEV_CTX_INVISIBLE); }

  bool is_composite_bindable() const {
    if (flags & (DEV_CTX_DEAD | DEV_CTX_INVISIBLE)) {
      return false;
    }
    if ((flags & DEV_CTX_BOUND) && !(flags & DEV_CTX_ALLOW_MULTI_COMPOSITE)) {
      return false;
    }
    return true;
  }

  void push_fragment(CompositeDeviceFragment* fragment) { fragments_.push_back(fragment); }
  bool is_fragments_empty() { return fragments_.is_empty(); }

  fbl::DoublyLinkedList<CompositeDeviceFragment*, CompositeDeviceFragment::DeviceNode>&
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
  void disassociate_from_composite() { composite_ = UnassociatedWithComposite{}; }

  void set_host(fbl::RefPtr<Devhost> host);
  const fbl::RefPtr<Devhost>& host() const { return host_; }
  uint64_t local_id() const { return local_id_; }

  const fbl::DoublyLinkedList<std::unique_ptr<Metadata>, Metadata::Node>& metadata() const {
    return metadata_;
  }
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
  // Remove tasks are used to facilitate |CompleteRemoval| requests.
  fbl::RefPtr<RemoveTask> GetActiveRemove() { return active_remove_; }

  // Run the completion for the outstanding unbind, if any.
  zx_status_t CompleteUnbind(zx_status_t status = ZX_OK);
  // Run the completion for the outstanding remove, if any.
  zx_status_t CompleteRemove(zx_status_t status = ZX_OK);

  // Drops the reference to the task.
  // This should be called if the device will not send an init, unbind or remove request.
  void DropInitTask() { active_init_ = nullptr; }
  void DropUnbindTask() { active_unbind_ = nullptr; }
  void DropRemoveTask() { active_remove_ = nullptr; }

  zx_status_t DriverCompatibiltyTest();

  zx::channel take_client_remote() { return std::move(client_remote_); }

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

  // TODO(teisenbe): We probably want more states.
  enum class State {
    kActive,
    kInitializing,  // The devhost is in the process of running the device init hook.
    kSuspending,    // The devhost is in the process of suspending the device.
    kSuspended,
    kResuming,   // The devhost is in the process of resuming the device.
    kResumed,    // Resume is complete. Will be marked active, after all children resume.
    kUnbinding,  // The devhost is in the process of unbinding and removing the device.
    kDead,       // The device has been remove()'d
  };

  void set_state(Device::State state) { state_ = state; }
  State state() const { return state_; }

  void clear_wait_make_visible() { wait_make_visible_ = false; }
  bool wait_make_visible() const { return wait_make_visible_; }

  void inc_num_removal_attempts() { num_removal_attempts_++; }
  size_t num_removal_attempts() const { return num_removal_attempts_; }

  enum class TestStateMachine {
    kTestNotStarted = 1,
    kTestUnbindSent,
    kTestBindSent,
    kTestBindDone,
    kTestSuspendSent,
    kTestSuspendDone,
    kTestResumeSent,
    kTestResumeDone,
    kTestDone,
  };

  TestStateMachine test_state() {
    fbl::AutoLock<fbl::Mutex> lock(&test_state_lock_);
    return test_state_;
  }

  void set_test_state(TestStateMachine new_state) {
    fbl::AutoLock<fbl::Mutex> lock(&test_state_lock_);
    test_state_ = new_state;
  }

  void clear_active_resume() { active_resume_ = nullptr; }
  void set_test_time(zx::duration& test_time) { test_time_ = test_time; }
  void set_test_reply_required(bool required) { test_reply_required_ = required; }
  zx::duration& test_time() { return test_time_; }
  const char* GetTestDriverName();
  zx::event& test_event() { return test_event_; }

  // This is public for testing purposes.
  std::unique_ptr<DriverTestReporter> test_reporter;

  zx_status_t set_test_output(zx::channel test_output, async_dispatcher_t* dispatcher) {
    test_output_ = std::move(test_output);
    test_wait_.set_object(test_output_.get());
    test_wait_.set_trigger(ZX_CHANNEL_PEER_CLOSED);
    return test_wait_.Begin(dispatcher);
  }

  const fidl::InterfacePtr<fuchsia::device::manager::DeviceController>& device_controller() const {
    return device_controller_;
  }

  const fidl::InterfaceRequest<fuchsia::device::manager::DeviceController> ConnectDeviceController(
      async_dispatcher_t* dispatcher) {
    return device_controller_.NewRequest(dispatcher);
  }

 private:
  void HandleTestOutput(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);

  // The driver sends output from run_unit_tests over this channel.
  zx::channel test_output_;

  // Async waiter that drives the consumption of test_output_. It is triggered when the channel is
  // closed by the driver, signalling the end of the tests. We don't print log messages until the
  // entire test is finished to avoid interleaving output from multiple drivers.
  async::WaitMethod<Device, &Device::HandleTestOutput> test_wait_{this};

  fidl::InterfacePtr<fuchsia::device::manager::DeviceController> device_controller_;

  zx_status_t HandleRead();
  int RunCompatibilityTests();

  const fbl::String name_;
  const fbl::String libname_;
  const fbl::String args_;

  fbl::RefPtr<Device> parent_;
  const uint32_t protocol_id_;

  fbl::RefPtr<Device> proxy_;

  fbl::Array<const zx_device_prop_t> props_;
  // If the device has a topological property in |props|, this points to it.
  const zx_device_prop_t* topo_prop_ = nullptr;

  async::TaskClosure publish_task_;

  // listnode for this device in its parent's list-of-children
  fbl::DoublyLinkedListNodeState<Device*> node_;

  // List of all child devices of this device, except for composite devices.
  // Composite devices are excluded because their multiple-parent nature
  // precludes using the same intrusive nodes as single-parent devices.
  fbl::DoublyLinkedList<Device*, Node> children_;

  // Metadata entries associated to this device.
  fbl::DoublyLinkedList<std::unique_ptr<Metadata>, Metadata::Node> metadata_;

  // listnode for this device in the all devices list
  fbl::DoublyLinkedListNodeState<fbl::RefPtr<Device>> all_devices_node_;

  // listnode for this device in its devhost's list-of-devices
  fbl::DoublyLinkedListNodeState<Device*> devhost_node_;

  // list of all fragments that this device bound to.
  fbl::DoublyLinkedList<CompositeDeviceFragment*, CompositeDeviceFragment::DeviceNode> fragments_;

  // - If this device is part of a composite device, this is inhabited by
  //   CompositeDeviceFragment* and it points to the fragment that matched it.
  //   Note that this is only set on the device that matched the fragment, not
  //   the "fragment device" added by the fragment driver.
  // - If this device is a composite device, this is inhabited by
  //   CompositeDevice* and it points to the composite that describes it.
  // - Otherwise, it is inhabited by UnassociatedWithComposite
  struct UnassociatedWithComposite {};
  std::variant<UnassociatedWithComposite, CompositeDevice*> composite_;

  fbl::RefPtr<Devhost> host_;
  // The id of this device from the perspective of the devhost.  This can be
  // used to communicate with the devhost about this device.
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

  // For attaching as an open connection to the proxy device,
  // or once the device becomes visible.
  zx::channel client_remote_;

  // If true, we should only make the device visible after DdkMakeVisible is called
  // (and after the init task has completed).
  bool wait_make_visible_ = false;

  // For compatibility tests.
  fbl::Mutex test_state_lock_;
  TestStateMachine test_state_ __TA_GUARDED(test_state_lock_) = TestStateMachine::kTestNotStarted;
  zx::event test_event_;
  zx::duration test_time_;
  fuchsia_device_manager_CompatibilityTestStatus test_status_;
  bool test_reply_required_ = false;

  // This lets us check for unexpected removals and is for testing use only.
  size_t num_removal_attempts_ = 0;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_H_
