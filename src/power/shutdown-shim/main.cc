// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/manager/llcpp/fidl.h>
#include <fuchsia/hardware/power/statecontrol/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/process/lifecycle/llcpp/fidl.h>
#include <fuchsia/sys2/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fit/function.h>
#include <zircon/process.h>
#include <zircon/status.h>

#include <chrono>
#include <thread>

#include <fbl/string_printf.h>
#include <fs/managed_vfs.h>
#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/vfs.h>

#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

namespace fio = ::llcpp::fuchsia::io;

namespace power_fidl = llcpp::fuchsia::hardware::power;
namespace device_manager_fidl = llcpp::fuchsia::device::manager;
namespace sys2_fidl = llcpp::fuchsia::sys2;

// The amount of time that the shim will spend trying to connect to
// power_manager before giving up.
// TODO(fxbug.dev/54426): increase this timeout
const zx::duration SERVICE_CONNECTION_TIMEOUT = zx::sec(2);

// The amount of time that the shim will spend waiting for a manually trigger
// system shutdown to finish before forcefully restarting the system.
const std::chrono::duration MANUAL_SYSTEM_SHUTDOWN_TIMEOUT = std::chrono::minutes(60);

class LifecycleServer final : public llcpp::fuchsia::process::lifecycle::Lifecycle::Interface {
 public:
  LifecycleServer(power_fidl::statecontrol::Admin::Interface::MexecCompleter::Async mexec_completer)
      : mexec_completer_(std::move(mexec_completer)) {}

  static zx_status_t Create(
      async_dispatcher_t* dispatcher,
      power_fidl::statecontrol::Admin::Interface::MexecCompleter::Async completer,
      zx::channel chan);

  void Stop(StopCompleter::Sync& completer) override;

 private:
  power_fidl::statecontrol::Admin::Interface::MexecCompleter::Async mexec_completer_;
};

zx_status_t LifecycleServer::Create(
    async_dispatcher_t* dispatcher,
    power_fidl::statecontrol::Admin::Interface::MexecCompleter::Async completer, zx::channel chan) {
  zx_status_t status = fidl::BindSingleInFlightOnly(
      dispatcher, std::move(chan), std::make_unique<LifecycleServer>(std::move(completer)));
  if (status != ZX_OK) {
    fprintf(stderr, "[shutdown-shim]: failed to bind lifecycle service: %s\n",
            zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

void LifecycleServer::Stop(StopCompleter::Sync& completer) {
  printf(
      "[shutdown-shim]: received shutdown command over lifecycle interface, completing the mexec "
      "call\n");
  mexec_completer_.ReplySuccess();
}

class StateControlAdminServer final : public power_fidl::statecontrol::Admin::Interface {
 public:
  StateControlAdminServer() : lifecycle_loop_((&kAsyncLoopConfigNoAttachToCurrentThread)) {}

  // Creates a new fs::Service backed by a new StateControlAdminServer, to be
  // inserted into a pseudo fs.
  static fbl::RefPtr<fs::Service> Create(async_dispatcher* dispatcher);

  void PowerFullyOn(
      power_fidl::statecontrol::Admin::Interface::PowerFullyOnCompleter::Sync& completer) override;

  void Reboot(
      power_fidl::statecontrol::RebootReason reboot_reason,
      power_fidl::statecontrol::Admin::Interface::RebootCompleter::Sync& completer) override;

  void RebootToBootloader(
      power_fidl::statecontrol::Admin::Interface::RebootToBootloaderCompleter::Sync& completer)
      override;

  void RebootToRecovery(power_fidl::statecontrol::Admin::Interface::RebootToRecoveryCompleter::Sync&
                            completer) override;

  void Poweroff(
      power_fidl::statecontrol::Admin::Interface::PoweroffCompleter::Sync& completer) override;

  void Mexec(power_fidl::statecontrol::Admin::Interface::MexecCompleter::Sync& completer) override;

  void SuspendToRam(
      power_fidl::statecontrol::Admin::Interface::SuspendToRamCompleter::Sync& completer) override;

 private:
  async::Loop lifecycle_loop_;
};

// Asynchronously connects to the given protocol.
zx_status_t connect_to_protocol(const char* name, zx::channel* local) {
  zx::channel remote;
  zx_status_t status = zx::channel::create(0, local, &remote);
  if (status != ZX_OK) {
    fprintf(stderr, "[shutdown-shim]: error creating channel: %s\n", zx_status_get_string(status));
    return status;
  }
  auto path = fbl::StringPrintf("/svc/%s", name);
  status = fdio_service_connect(path.data(), remote.release());
  if (status != ZX_OK) {
    printf("[shutdown-shim]: failed to connect to %s: %s\n", name, zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

// Opens a service node, failing if the provider of the service does not respond
// to messages within SERVICE_CONNECTION_TIMEOUT.
//
// This is accomplished by opening the service node, writing an invalid message
// to the channel, and observing PEER_CLOSED within the timeout. This is testing
// that something is responding to open requests for this service, as opposed to
// the intended provider for this service being stuck on component resolution
// indefinitely, which causes connection attempts to the component to never
// succeed nor fail. By observing a PEER_CLOSED, we can ensure that the service
// provider received our message and threw it out (or the provider doesn't
// exist). Upon receiving the PEER_CLOSED, we then open a new connection and
// save it in `local`.
//
// This is protecting against packaged components being stuck in resolution for
// forever, which happens if pkgfs never starts up (this always happens on
// bringup). Once a component is able to be resolved, then all new service
// connections will either succeed or fail rather quickly.
zx_status_t connect_to_protocol_with_timeout(const char* name, zx::channel* local) {
  zx::channel local_2;
  zx_status_t status = connect_to_protocol(name, &local_2);
  if (status != ZX_OK) {
    return status;
  }

  // We want to use the zx_channel_call syscall directly here, because there's
  // no way to set the timeout field on the call using the FIDL bindings.
  char garbage_data[6] = {0, 1, 2, 3, 4, 5};
  zx_channel_call_args_t call = {
      // Bytes to send in the channel call
      .wr_bytes = garbage_data,
      // Handles to send in the channel call
      .wr_handles = nullptr,
      // Buffer to write received bytes into from the channel call
      .rd_bytes = nullptr,
      // Buffer to write received handles into from the channel call
      .rd_handles = nullptr,
      // Number of bytes to send
      .wr_num_bytes = 6,
      // Number of bytes we can receive
      .rd_num_bytes = 0,
      // Number of handles we can receive
      .rd_num_handles = 0,
  };
  uint32_t actual_bytes, actual_handles;
  status = local_2.call(0, zx::deadline_after(SERVICE_CONNECTION_TIMEOUT), &call, &actual_bytes,
                        &actual_handles);
  if (status == ZX_ERR_TIMED_OUT) {
    fprintf(stderr, "[shutdown-shim]: timed out connecting to %s\n", name);
    return status;
  }
  if (status != ZX_ERR_PEER_CLOSED) {
    fprintf(stderr, "[shutdown-shim]: unexpected response from %s: %s\n", name,
            zx_status_get_string(status));
    return status;
  }
  return connect_to_protocol(name, local);
}

// Connect to fuchsia.device.manager.SystemStateTransition and set the
// termination state.
zx_status_t set_system_state_transition_behavior(power_fidl::statecontrol::SystemPowerState state) {
  zx::channel local;
  zx_status_t status =
      connect_to_protocol(&device_manager_fidl::SystemStateTransition::Name[0], &local);
  if (status != ZX_OK) {
    fprintf(stderr, "[shutdown-shim]: error connecting to driver_manager\n");
    return status;
  }
  auto system_state_transition_behavior_client =
      device_manager_fidl::SystemStateTransition::SyncClient(std::move(local));

  auto resp = system_state_transition_behavior_client.SetTerminationSystemState(state);
  if (resp.status() != ZX_OK) {
    fprintf(stderr, "[shutdown-shim]: transport error sending message to driver_manager: %s\n",
            resp.error());
    return resp.status();
  }
  if (resp->result.is_err()) {
    return resp->result.err();
  }
  return ZX_OK;
}

// Connect to fuchsia.sys2.SystemController and initiate a system shutdown. If
// everything goes well, this function shouldn't return until shutdown is
// complete.
zx_status_t initiate_component_shutdown() {
  zx::channel local;
  zx_status_t status = connect_to_protocol(&sys2_fidl::SystemController::Name[0], &local);
  if (status != ZX_OK) {
    fprintf(stderr, "[shutdown-shim]: error connecting to component_manager\n");
    return status;
  }
  auto system_controller_client = sys2_fidl::SystemController::SyncClient(std::move(local));

  auto resp = system_controller_client.Shutdown();
  return resp.status();
}

// Sleeps for MANUAL_SYSTEM_SHUTDOWN_TIMEOUT, and then exits the process
void shutdown_timer() {
  std::this_thread::sleep_for(MANUAL_SYSTEM_SHUTDOWN_TIMEOUT);

  // We shouldn't still be running at this point

  exit(1);
}

// Manually drive a shutdown by setting state as driver_manager's termination
// behavior and then instructing component_manager to perform an orderly
// shutdown of components. If the orderly shutdown takes too long the shim will
// exit with a non-zero exit code, killing the root job.
void drive_shutdown_manually(power_fidl::statecontrol::SystemPowerState state) {
  printf("[shutdown-shim]: driving shutdown manually\n");

  // Start a new thread that makes us exit uncleanly after a timeout. This will
  // guarantee that shutdown doesn't take longer than
  // MANUAL_SYSTEM_SHUTDOWN_TIMEOUT, because we're marked as critical to the
  // root job and us exiting will bring down userspace and cause a reboot.
  std::thread(shutdown_timer).detach();

  zx_status_t status = set_system_state_transition_behavior(state);
  if (status != ZX_OK) {
    fprintf(stderr,
            "[shutdown-shim]: error setting system state transition behavior in driver_manager, "
            "proceeding with component shutdown anyway: %s\n",
            zx_status_get_string(status));
    // Proceed here, maybe we can at least gracefully reboot still
    // (driver_manager's default behavior)
  }

  status = initiate_component_shutdown();
  if (status != ZX_OK) {
    fprintf(
        stderr,
        "[shutdown-shim]: error initiating component shutdown, system shutdown impossible: %s\n",
        zx_status_get_string(status));
    // Recovery from this state is impossible. Exit with a non-zero exit code,
    // so our critical marking causes the system to forcefully restart.
    exit(1);
  }
  fprintf(stderr, "[shutdown-shim]: manual shutdown successfully initiated\n");
}

zx_status_t send_command(power_fidl::statecontrol::Admin::SyncClient statecontrol_client,
                         power_fidl::statecontrol::SystemPowerState fallback_state,
                         power_fidl::statecontrol::RebootReason* reboot_reason) {
  switch (fallback_state) {
    case power_fidl::statecontrol::SystemPowerState::REBOOT: {
      if (reboot_reason == nullptr) {
        fprintf(stderr, "[shutdown-shim]: internal error, bad pointer to reason for reboot\n");
        return ZX_ERR_INTERNAL;
      }
      auto resp = statecontrol_client.Reboot(*reboot_reason);
      if (resp.status() != ZX_OK) {
        return ZX_ERR_UNAVAILABLE;
      } else if (resp->result.is_err()) {
        return resp->result.err();
      } else {
        return ZX_OK;
      }
    } break;
    case power_fidl::statecontrol::SystemPowerState::REBOOT_BOOTLOADER: {
      auto resp = statecontrol_client.RebootToBootloader();
      if (resp.status() != ZX_OK) {
        return ZX_ERR_UNAVAILABLE;
      } else if (resp->result.is_err()) {
        return resp->result.err();
      } else {
        return ZX_OK;
      }
    } break;
    case power_fidl::statecontrol::SystemPowerState::REBOOT_RECOVERY: {
      auto resp = statecontrol_client.RebootToRecovery();
      if (resp.status() != ZX_OK) {
        return ZX_ERR_UNAVAILABLE;
      } else if (resp->result.is_err()) {
        return resp->result.err();
      } else {
        return ZX_OK;
      }
    } break;
    case power_fidl::statecontrol::SystemPowerState::POWEROFF: {
      auto resp = statecontrol_client.Poweroff();
      if (resp.status() != ZX_OK) {
        return ZX_ERR_UNAVAILABLE;
      } else if (resp->result.is_err()) {
        return resp->result.err();
      } else {
        return ZX_OK;
      }
    } break;
    case power_fidl::statecontrol::SystemPowerState::MEXEC: {
      auto resp = statecontrol_client.Mexec();
      if (resp.status() != ZX_OK) {
        return ZX_ERR_UNAVAILABLE;
      } else if (resp->result.is_err()) {
        return resp->result.err();
      } else {
        return ZX_OK;
      }
    } break;
    case power_fidl::statecontrol::SystemPowerState::SUSPEND_RAM: {
      auto resp = statecontrol_client.SuspendToRam();
      if (resp.status() != ZX_OK) {
        return ZX_ERR_UNAVAILABLE;
      } else if (resp->result.is_err()) {
        return resp->result.err();
      } else {
        return ZX_OK;
      }
    } break;
    default:
      return ZX_ERR_INTERNAL;
  }
}

// Connects to power_manager and passes a SyncClient to the given function. The
// function is expected to return an error if there was a transport-related
// issue talking to power_manager, in which case this program will talk to
// driver_manager and component_manager to drive shutdown manually.
zx_status_t forward_command(power_fidl::statecontrol::SystemPowerState fallback_state,
                            power_fidl::statecontrol::RebootReason* reboot_reason) {
  printf("[shutdown-shim]: checking power_manager liveness\n");
  zx::channel local;
  zx_status_t status =
      connect_to_protocol_with_timeout(&power_fidl::statecontrol::Admin::Name[0], &local);
  if (status == ZX_OK) {
    printf("[shutdown-shim]: trying to forward command\n");
    status = send_command(power_fidl::statecontrol::Admin::SyncClient(std::move(local)),
                          fallback_state, reboot_reason);
    if (status != ZX_ERR_UNAVAILABLE) {
      return status;
    }
  }

  printf("[shutdown-shim]: failed to forward command to power_manager: %s\n",
         zx_status_get_string(status));

  drive_shutdown_manually(fallback_state);

  // We should block on fuchsia.sys.SystemController forever on this thread, if
  // it returns something has gone wrong.
  fprintf(stderr, "[shutdown-shim]: we shouldn't still be running, crashing the system\n");
  exit(1);
}

zx_status_t forward_command(power_fidl::statecontrol::SystemPowerState fallback_state) {
  return forward_command(fallback_state, nullptr);
}

void StateControlAdminServer::PowerFullyOn(
    power_fidl::statecontrol::Admin::Interface::PowerFullyOnCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void StateControlAdminServer::Reboot(
    power_fidl::statecontrol::RebootReason reboot_reason,
    power_fidl::statecontrol::Admin::Interface::RebootCompleter::Sync& completer) {
  zx_status_t status =
      forward_command(power_fidl::statecontrol::SystemPowerState::REBOOT, &reboot_reason);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void StateControlAdminServer::RebootToBootloader(
    power_fidl::statecontrol::Admin::Interface::RebootToBootloaderCompleter::Sync& completer) {
  zx_status_t status =
      forward_command(power_fidl::statecontrol::SystemPowerState::REBOOT_BOOTLOADER);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void StateControlAdminServer::RebootToRecovery(
    power_fidl::statecontrol::Admin::Interface::RebootToRecoveryCompleter::Sync& completer) {
  zx_status_t status = forward_command(power_fidl::statecontrol::SystemPowerState::REBOOT_RECOVERY);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void StateControlAdminServer::Poweroff(
    power_fidl::statecontrol::Admin::Interface::PoweroffCompleter::Sync& completer) {
  zx_status_t status = forward_command(power_fidl::statecontrol::SystemPowerState::POWEROFF);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void StateControlAdminServer::Mexec(
    power_fidl::statecontrol::Admin::Interface::MexecCompleter::Sync& completer) {
  zx::channel local;
  zx_status_t status =
      connect_to_protocol_with_timeout(&power_fidl::statecontrol::Admin::Name[0], &local);
  if (status == ZX_OK) {
    status = send_command(power_fidl::statecontrol::Admin::SyncClient(std::move(local)),
                          power_fidl::statecontrol::SystemPowerState::MEXEC, nullptr);
    if (status == ZX_OK) {
      completer.ReplySuccess();
      return;
    } else if (status != ZX_ERR_UNAVAILABLE) {
      completer.ReplyError(status);
      return;
    }
  }

  printf("[shutdown-shim]: failed to forward mexec command to power_manager: %s\n",
         zx_status_get_string(status));

  // The mexec command will cause driver_manager to safely terminate, and _not_
  // turn the system off. This will result in shutdown progressing to the
  // shutdown shim. Once it reaches us we know that all drivers and filesystems
  // are parked, so we can return the mexec call, at which point the client will
  // make the mexec syscall.
  //
  // Start a new lifecycle server with the completer so that it can respond to
  // the client once we're told to terminate. Do this on a separate thread
  // because this one will be blocked on the fuchsia.sys2.SystemController call.
  zx::channel lifecycle_request(zx_take_startup_handle(PA_LIFECYCLE));
  if (!lifecycle_request.is_valid()) {
    printf("[shutdown-shim]: missing lifecycle handle, mexec must have already been called\n");
    completer.ReplyError(ZX_ERR_INTERNAL);
    return;
  }

  status = LifecycleServer::Create(lifecycle_loop_.dispatcher(), completer.ToAsync(),
                                   std::move(lifecycle_request));
  if (status != ZX_OK) {
    fprintf(stderr, "[shutdown-shim]: failed to start lifecycle server: %d\n", status);
    exit(status);
  }

  lifecycle_loop_.StartThread("lifecycle");

  drive_shutdown_manually(power_fidl::statecontrol::SystemPowerState::MEXEC);

  // We should block on fuchsia.sys.SystemController forever on this thread, if
  // it returns something has gone wrong.
  fprintf(stderr, "[shutdown-shim]: we shouldn't still be running, crashing the system\n");
  exit(1);
}

void StateControlAdminServer::SuspendToRam(
    power_fidl::statecontrol::Admin::Interface::SuspendToRamCompleter::Sync& completer) {
  zx_status_t status = forward_command(power_fidl::statecontrol::SystemPowerState::SUSPEND_RAM);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

fbl::RefPtr<fs::Service> StateControlAdminServer::Create(async_dispatcher* dispatcher) {
  return fbl::MakeRefCounted<fs::Service>([dispatcher](zx::channel chan) mutable {
    zx_status_t status = fidl::BindSingleInFlightOnly(dispatcher, std::move(chan),
                                                      std::make_unique<StateControlAdminServer>());
    if (status != ZX_OK) {
      fprintf(stderr, "[shutdown-shim] failed to bind statecontrol.Admin service: %s\n",
              zx_status_get_string(status));
      return status;
    }
    return ZX_OK;
  });
}

int main() {
  zx_status_t status = StdoutToDebuglog::Init();
  if (status != ZX_OK) {
    return status;
  }
  printf("[shutdown-shim]: started\n");

  async::Loop loop((async::Loop(&kAsyncLoopConfigAttachToCurrentThread)));

  fs::ManagedVfs outgoing_vfs((loop.dispatcher()));
  auto outgoing_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  auto svc_dir = fbl::MakeRefCounted<fs::PseudoDir>();

  svc_dir->AddEntry(power_fidl::statecontrol::Admin::Name,
                    StateControlAdminServer::Create(loop.dispatcher()));
  outgoing_dir->AddEntry("svc", std::move(svc_dir));

  outgoing_vfs.ServeDirectory(outgoing_dir,
                              zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST)));

  loop.Run();

  fprintf(stderr, "[shutdown-shim]: exited unexpectedly\n");
  return 1;
}
