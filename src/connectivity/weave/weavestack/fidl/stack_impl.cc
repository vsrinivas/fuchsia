// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stack_impl.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <string>

// clang-format off
#pragma GCC diagnostic push
#include <Weave/DeviceLayer/PlatformManager.h>
#include <Weave/DeviceLayer/ConfigurationManager.h>
#pragma GCC diagnostic pop
// clang-format on

#include "stack_utils.h"

namespace weavestack {
namespace {

using fuchsia::weave::ErrorCode;
using fuchsia::weave::HostPort;
using fuchsia::weave::PairingState;
using fuchsia::weave::PairingStateWatcher;
using fuchsia::weave::QrCode;
using fuchsia::weave::ResetConfigFlags;
using fuchsia::weave::Stack_GetQrCode_Response;
using fuchsia::weave::Stack_GetQrCode_Result;
using fuchsia::weave::Stack_ResetConfig_Response;
using fuchsia::weave::Stack_ResetConfig_Result;
using fuchsia::weave::SvcDirectoryWatcher;

using nl::Weave::DeviceLayer::ConfigurationMgr;
using nl::Weave::DeviceLayer::PlatformMgrImpl;
using nl::Weave::DeviceLayer::WeaveDeviceEvent;
using nl::Weave::DeviceLayer::DeviceEventType::kAccountPairingChange;
using nl::Weave::DeviceLayer::DeviceEventType::kFabricMembershipChange;
using nl::Weave::DeviceLayer::DeviceEventType::kServiceProvisioningChange;
using nl::Weave::DeviceLayer::DeviceEventType::kServiceTunnelStateChange;
using nl::Weave::Profiles::DeviceControl::DeviceControlDelegate;

// Size of the buffer for retrieving the QR code.
constexpr size_t kQrCodeBufSize = fuchsia::weave::MAX_QR_CODE_SIZE + 1;
// Size of the buffer for retrieving hostnames.
constexpr size_t kHostnameBufferSize = fuchsia::net::MAX_HOSTNAME_SIZE + 1;
}  // namespace

// Watcher class declarations --------------------------------------------------

class StackImpl::PairingStateWatcherImpl : public PairingStateWatcher {
 public:
  explicit PairingStateWatcherImpl(StackImpl* stack);

  /// Returns the state of pairing
  ///
  /// First call returns the current pairing state or blocks until the pairing
  /// state is available. Subsequent calls will block until the pairing state
  /// changes.
  void WatchPairingState(WatchPairingStateCallback callback) override;

  /// Notify that pairing state has changed.
  zx_status_t Notify();

 private:
  /// Perform the callback if there is an active watcher.
  zx_status_t DoCallback();

  // Prevent copy construction.
  PairingStateWatcherImpl(const PairingStateWatcherImpl&) = delete;
  // Prevent copy assignment.
  PairingStateWatcherImpl& operator=(const PairingStateWatcherImpl&) = delete;

  StackImpl* stack_;
  bool dirty_;
  bool first_call_;
  WatchPairingStateCallback pairing_state_callback_;
};

class StackImpl::SvcDirectoryWatcherImpl : public SvcDirectoryWatcher {
 public:
  explicit SvcDirectoryWatcherImpl(StackImpl* stack, uint64_t endpoint_id);

  /// Returns a vector of HostPorts for the watched endpoint ID.
  ///
  /// First call returns the current list of HostPorts or blocks until the list
  /// is available from the service. Subsequent calls will block until a new
  /// ServiceDirectory lookup is made and will return the list associated with
  /// the watched endpoint ID, which may or may not be the same as prior values.
  void WatchServiceDirectory(WatchServiceDirectoryCallback callback) override;

  /// Notify that watched service directory has changed.
  zx_status_t Notify();

 private:
  /// Perform the callback if there is an active watcher.
  zx_status_t DoCallback();

  // Prevent copy construction.
  SvcDirectoryWatcherImpl(const SvcDirectoryWatcherImpl&) = delete;
  // Prevent copy assignment.
  SvcDirectoryWatcherImpl& operator=(const SvcDirectoryWatcherImpl&) = delete;

  StackImpl* stack_;
  bool dirty_;
  bool first_call_;
  uint64_t endpoint_id_;
  WatchServiceDirectoryCallback svc_directory_callback_;
};

// StackImpl definitions -------------------------------------------------------

StackImpl::StackImpl(sys::ComponentContext* context) : context_(context) {}

zx_status_t StackImpl::Init() {
  zx_status_t status = ZX_OK;
  WEAVE_ERROR err = WEAVE_NO_ERROR;

  // Register with the context.
  status = context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to register StackImpl handler with status ="
                   << zx_status_get_string(status);
    return status;
  }

  // Register event handler with Weave Device Layer.
  err = PlatformMgrImpl().AddEventHandler(TrampolineEvent, reinterpret_cast<intptr_t>(this));
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << "Failed to register event handler with device layer: " << nl::ErrorStr(err);
    return ZX_ERR_INTERNAL;
  }

  return status;
}

// The destructor must be defined after the full definitions of the watcher
// classes to avoid attempting to use an incomplete definition in the compiler
// generated default destructor.
StackImpl::~StackImpl() = default;

void StackImpl::GetPairingStateWatcher(fidl::InterfaceRequest<PairingStateWatcher> watcher) {
  pairing_state_watchers_.AddBinding(std::make_unique<PairingStateWatcherImpl>(this),
                                     std::move(watcher));
}

void StackImpl::GetSvcDirectoryWatcher(uint64_t endpoint_id,
                                       fidl::InterfaceRequest<SvcDirectoryWatcher> watcher) {
  svc_directory_watchers_.AddBinding(std::make_unique<SvcDirectoryWatcherImpl>(this, endpoint_id),
                                     std::move(watcher));
}

void StackImpl::GetQrCode(GetQrCodeCallback callback) {
  QrCode qr_code;
  Stack_GetQrCode_Response response;
  Stack_GetQrCode_Result result;
  WEAVE_ERROR err;

  // Initialize space for the QR code.
  response.qr_code.data.resize(kQrCodeBufSize, '\0');
  // Copy the QR code into qr_code.data.
  err = ConfigurationMgr().GetQRCodeString(response.qr_code.data.data(),
                                           response.qr_code.data.size());
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << "Failed to retrieve QR code: " << err;
    result.set_err(ErrorCode::UNSPECIFIED_ERROR);
  } else {
    // Resize to returned length.
    response.qr_code.data.resize(strnlen(response.qr_code.data.data(), kQrCodeBufSize));
    result.set_response(response);
  }

  callback(std::move(result));
}

void StackImpl::ResetConfig(ResetConfigFlags flags, ResetConfigCallback callback) {
  Stack_ResetConfig_Response response;
  Stack_ResetConfig_Result result;

  WEAVE_ERROR err = GetDeviceControl().OnResetConfig(static_cast<uint16_t>(flags));
  if (err != WEAVE_NO_ERROR) {
    result.set_err(ErrorCode::UNSPECIFIED_ERROR);
  } else {
    result.set_response(response);
  }

  callback(std::move(result));
}

void StackImpl::NotifyPairingState() {
  last_pairing_state_ = std::make_unique<PairingState>(CurrentPairingState());
  for (auto& binding : pairing_state_watchers_.bindings()) {
    binding->impl()->Notify();
  }
}

void StackImpl::NotifySvcDirectory() {
  for (auto& binding : svc_directory_watchers_.bindings()) {
    binding->impl()->Notify();
  }
}

void StackImpl::HandleWeaveDeviceEvent(const WeaveDeviceEvent* event) {
  // Handle events.
  switch (event->Type) {
    case kServiceTunnelStateChange:
      if (event->ServiceTunnelStateChange.Result ==
          nl::Weave::DeviceLayer::kConnectivity_Established) {
        // New tunnel established, notify service directory watchers to check
        // for updates to their service directory entries.
        NotifySvcDirectory();
      }
      // TODO(fxbug.dev/52935): Add event for Thread provisioning.
      // TODO(fxbug.dev/52936): Add event for WiFi provisioning.
      __FALLTHROUGH;
    case kFabricMembershipChange:
    case kServiceProvisioningChange:
    case kAccountPairingChange:
      // Pairing/provisioning state has changed, notify watchers.
      NotifyPairingState();
      break;
    default:
      // Ignore all other events.
      break;
  }
}

void StackImpl::TrampolineEvent(const WeaveDeviceEvent* event, intptr_t arg) {
  StackImpl* self = reinterpret_cast<StackImpl*>(arg);
  self->HandleWeaveDeviceEvent(event);
}

DeviceControlDelegate& StackImpl::GetDeviceControl() {
  return PlatformMgrImpl().GetDeviceControl();
}

zx_status_t StackImpl::LookupHostPorts(uint64_t endpoint_id, std::vector<HostPort>* host_ports) {
  WEAVE_ERROR err = WEAVE_NO_ERROR;
  nl::Weave::HostPortList host_port_list;

  // Lookup host endpoints from service directory.
  err = PlatformMgrImpl().GetServiceDirectoryManager().lookup(endpoint_id, &host_port_list);
  if (err == WEAVE_ERROR_INVALID_SERVICE_EP) {
    FX_LOGS(WARNING) << "Invalid service directory endpoint: " << endpoint_id;
    return ZX_ERR_INVALID_ARGS;
  }
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << "Failed to lookup service directory: " << nl::ErrorStr(err);
    return ZX_ERR_INTERNAL;
  }

  while (!host_port_list.IsEmpty()) {
    HostPort host_port;
    char hostname_buf[kHostnameBufferSize] = {};
    uint16_t port = 0;

    // Extract HostPort info from host_port_list.
    err = host_port_list.Pop(hostname_buf, kHostnameBufferSize, port);
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "Failed to extract host/port in HostPortList: " << nl::ErrorStr(err);
      return ZX_ERR_INTERNAL;
    }

    // Convert to FIDL HostPort.
    host_port.host = HostFromHostname(hostname_buf);
    host_port.port = port;

    // Push onto returned HostPort vector.
    host_ports->emplace_back(std::move(host_port));
  }

  return ZX_OK;
}

// Watcher class definitions ---------------------------------------------------

StackImpl::PairingStateWatcherImpl::PairingStateWatcherImpl(StackImpl* stack)
    : stack_(stack), dirty_(false), first_call_(true) {}

void StackImpl::PairingStateWatcherImpl::WatchPairingState(WatchPairingStateCallback callback) {
  // Check to make sure there isn't a waiting callback already.
  if (pairing_state_callback_) {
    stack_->pairing_state_watchers_.CloseBinding(this, ZX_ERR_BAD_STATE);
    return;
  }

  // Save callback for sending result (potentially async).
  pairing_state_callback_ = std::move(callback);
  // Callback to caller if data is ready.
  DoCallback();
}

zx_status_t StackImpl::PairingStateWatcherImpl::Notify() {
  // Mark dirty.
  dirty_ = true;
  // Perform callback if there is a waiting client.
  return DoCallback();
}

zx_status_t StackImpl::PairingStateWatcherImpl::DoCallback() {
  zx_status_t status = ZX_OK;
  PairingState pairing_state;

  // Only perform callback if ready.
  if (!pairing_state_callback_ || !(dirty_ || first_call_)) {
    return ZX_OK;
  }

  // Allocate pairing state if not present.
  if (!stack_->last_pairing_state_) {
    stack_->last_pairing_state_ = std::make_unique<PairingState>(CurrentPairingState());
  }

  // Reset for next callback.
  WatchPairingStateCallback callback = std::move(pairing_state_callback_);
  dirty_ = false;
  first_call_ = false;

  // Copy the pairing state into return value.
  status = stack_->last_pairing_state_->Clone(&pairing_state);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to clone pairing state: " << zx_status_get_string(status)
                   << ", cannot send callback!";
    stack_->pairing_state_watchers_.CloseBinding(this, status);
    return status;
  }

  // Send updated pairing state to caller.
  callback(std::move(pairing_state));
  return ZX_OK;
}

StackImpl::SvcDirectoryWatcherImpl::SvcDirectoryWatcherImpl(StackImpl* stack, uint64_t endpoint_id)
    : stack_(stack), dirty_(false), first_call_(true), endpoint_id_(endpoint_id) {}

void StackImpl::SvcDirectoryWatcherImpl::WatchServiceDirectory(
    WatchServiceDirectoryCallback callback) {
  // Check to make sure there isn't a waiting callback already.
  if (svc_directory_callback_) {
    stack_->svc_directory_watchers_.CloseBinding(this, ZX_ERR_BAD_STATE);
    return;
  }

  // Save callback for sending result (potentially async).
  svc_directory_callback_ = std::move(callback);
  // Attempt to reply if first call or dirty data.
  DoCallback();
}

zx_status_t StackImpl::SvcDirectoryWatcherImpl::Notify() {
  // Mark dirty.
  dirty_ = true;
  // Perform callback if there is a waiting client.
  return DoCallback();
}

zx_status_t StackImpl::SvcDirectoryWatcherImpl::DoCallback() {
  std::vector<HostPort> host_ports;

  if (!svc_directory_callback_ || !(dirty_ || first_call_)) {
    return ZX_OK;
  }

  // Reset for next callback.
  WatchServiceDirectoryCallback callback = std::move(svc_directory_callback_);
  dirty_ = false;
  first_call_ = false;

  // Lookup the (potentially new) HostPorts.
  zx_status_t status = stack_->LookupHostPorts(endpoint_id_, &host_ports);
  if (status != ZX_OK) {
    stack_->svc_directory_watchers_.CloseBinding(this, status);
    return status;
  }

  // Call callback.
  first_call_ = false;
  callback(std::move(host_ports));

  return ZX_OK;
}

}  // namespace weavestack
