// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V2_SHUTDOWN_MANAGER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V2_SHUTDOWN_MANAGER_H_

#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <fidl/fuchsia.process.lifecycle/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/directory.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>

#include "src/devices/bin/driver_manager/v2/node_remover.h"

namespace dfv2 {
using fuchsia_hardware_power_statecontrol::wire::SystemPowerState;

// Theory of operation of ShutdownManager:
//  There are a number of ways shutdown can be initiated:
//   - The process could be terminated, resulting in a signal from the Lifecycle channel
//   - The administrator interface could signal UnregisterSystemStorageForShutdown, or
//     SuspendWithoutExit
//   - Any of the three fidl connections could be dropped
//  If any of these evens happen, the shutdown procedure should be started, if it is not
//  already in progress.
//  The state transition table is then:
//
//  [kRunning]
//      |
//  StartShutdown <--- Some event that triggers shutdown
//     \|/
//  [kPackageStopping]---OnPackageShutdownComplete
//                                 \|/
//                            [kBootStopping] ----OnBootShutdownComplete
//                                                         \|/
//                                                     [kStopped]
//
//  OnPackageShutdownComplete and OnBootShutdownComplete are callbacks from the entity in charge
//  of shutting down drivers.  Shutdown triggering events that occur while shutdown is in progress
//  have no effect on the shutdown process, although some events may cause an error to be logged.
//  After shutting down the package and boot drivers, the system is signalled to stop in some
//  manner, dictated by what is set by SetTerminationSystemState.  The default state, which is
//  invoked if there is some error, is REBOOT.
//  Any errors in the shutdown process are logged, but ulimately do not stop the shutdown.
//  SetTerminationSystemState and SetMexecZbis are accepted in all stages except STOPPED
//  The ShutdownManager is not thread safe. It assumes that all channels will be dispatched
//  on the same single threaded dispatcher, and that all callbacks will also be called on
//  that same thread.
class ShutdownManager : public fidl::WireServer<fuchsia_device_manager::Administrator>,
                        public fidl::WireServer<fuchsia_process_lifecycle::Lifecycle>,
                        public fidl::WireServer<fuchsia_device_manager::SystemStateTransition> {
 public:
  enum class State : uint32_t {
    // The system is running, nothing is being stopped.
    kRunning = 0u,
    // The devices whose's drivers live in storage are stopped or in the middle of being
    // stopped.
    kPackageStopping = 1u,
    // The entire system is in the middle of being stopped.
    kBootStopping = 2u,
    // The entire system is stopped.
    kStopped = 2u,
  };

  ShutdownManager(NodeRemover* node_remover, async_dispatcher_t* dispatcher);

  void Publish(component::OutgoingDirectory& outgoing,
               fidl::ClientEnd<fuchsia_io::Directory> dev_io);

  // Called by the node_remover when it finishes removing drivers in storage.
  // Should only be called when in state: kPackageStopping.
  // This function will transition the state to State::kBootStopping.
  void OnPackageShutdownComplete();

  // Called by the node_remover when it finishes removing boot drivers.
  // Should only be called when in state: kBootStopping.
  // This function will transition the state to State::kStopped.
  void OnBootShutdownComplete();

 private:
  // fuchsia.device.manager/Administrator interface
  // TODO(fxbug.dev/68529): Remove this API.
  // This is a temporary API until DriverManager can ensure that base drivers
  // will be shut down automatically before fshost exits. This will happen
  // once drivers-as-components is implemented.
  // In the meantime, this API should only be called by fshost, and it must
  // be called before fshost exits. This function iterates over the devices
  // and suspends any device whose driver lives in storage. This API must be
  // called by fshost before it shuts down. Otherwise the devices that live
  // in storage may page fault as it access memory that should be provided by
  // the exited fshost. This function will not return until the devices are
  // suspended. If there are no devices that live in storage, this function
  // will immediatetly return.
  void UnregisterSystemStorageForShutdown(
      UnregisterSystemStorageForShutdownCompleter::Sync& completer) override;

  // Tell DriverManager to go through the suspend process, but don't exit
  // afterwards. This is used in tests to check that suspend works correctly.
  void SuspendWithoutExit(SuspendWithoutExitCompleter::Sync& completer) override;

  // fuchsia.process.lifecycle/Lifecycle interface
  // The process must clean up its state in preparation for termination, and
  // must close the channel hosting the `Lifecycle` protocol when it is
  // ready to be terminated. The process should exit after it completes its
  // cleanup. At the discretion of the system the process may be terminated
  // before it closes the `Lifecycle` channel.
  void Stop(StopCompleter::Sync& completer) override;

  // fuchsia.device.manager/SystemStateTransition interface
  // Sets and updates the termination SystemPowerState of driver_manager.
  // On Success, the system power state is cached. The next time
  // driver_manager's stop event is triggered, driver_manager suspends
  // the system to "state".
  // Returns ZX_ERR_INVALID_ARGS if the system power state is not a shutdown/reboot
  // state(POWEROFF, REBOOT, REBOOT_BOOTLOADER, REBOOT_RECOVERY, MEXEC)
  // Returns ZX_ERR_BAD_STATE if driver_manager is unable to save the state.
  // Each time the api is called the termination state is updated and cached.
  void SetTerminationSystemState(SetTerminationSystemStateRequestView request,
                                 SetTerminationSystemStateCompleter::Sync& completer) override;

  // When the system termination state is MEXEC, in the course of shutting
  // down, driver_manager will perform an mexec itself after suspending all
  // drivers.
  // This method prepares for an MEXEC shutdown, stashing the kernel and
  // data ZBIs to later be passed to zx_system_mexec(). This method does
  // not affect termination state itself.
  // The ZBI items specified by `zx_system_mexec_payload_get()` will be appended
  // to the provided data ZBI.
  //
  // Returns
  // * ZX_ERR_INVALID_ARGS: if either VMO handle is invalid;
  // * ZX_ERR_IO_DATA_INTEGRITY: if any ZBI format or storage access errors are
  //   encountered;
  // * any status returned by `zx_system_mexec_payload_get()`.
  void SetMexecZbis(SetMexecZbisRequestView request,
                    SetMexecZbisCompleter::Sync& completer) override;

  // Start the shutdown procedure
  // This should only be called once.  This will transition the state:
  // State::kRunning -> State::kPackageStopping
  // The caller must ensure that shutdown_state_ == State::kRunning before calling.
  void StartShutdown();

  // Execute the shutdown strategy set in shutdown_system_state_.
  // This should be done after all attempts at shutting down drivers has been made.
  void SystemExecute();

  // Called when one of our connections is dropped.
  void OnUnbound(const char* connection, fidl::UnbindInfo info);

  // The driver runner should always be valid while the shutdown manager exists.
  // TODO(fxbug.dev/114374): ensure that this pointer is valid
  NodeRemover* node_remover_;

  std::optional<fidl::ServerBindingRef<fuchsia_process_lifecycle::Lifecycle>> lifecycle_binding_;
  std::optional<fidl::ServerBindingRef<fuchsia_device_manager::SystemStateTransition>>
      sys_state_binding_;
  std::optional<UnregisterSystemStorageForShutdownCompleter::Async> unregister_storage_completer_;
  std::optional<StopCompleter::Async> stop_completer_;

  // The type of shutdown to perform.  Default to REBOOT, in the case of errors, channel closing
  SystemPowerState shutdown_system_state_ = SystemPowerState::kReboot;
  State shutdown_state_ = State::kRunning;
  zx::vmo mexec_kernel_zbi_, mexec_data_zbi_;
  async_dispatcher_t* dispatcher_;
  zx::resource mexec_resource_, power_resource_;
  // Tracks if we received a stop signal from the fuchsia_process_lifecycle::Lifecycle channel.
  bool lifecycle_stop_ = false;
};

}  // namespace dfv2
#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V2_SHUTDOWN_MANAGER_H_
