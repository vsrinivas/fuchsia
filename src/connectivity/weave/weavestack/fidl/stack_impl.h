// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_WEAVESTACK_FIDL_STACK_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_WEAVESTACK_FIDL_STACK_IMPL_H_

#include <fuchsia/weave/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#pragma GCC diagnostic push
#include <Weave/DeviceLayer/WeaveDeviceEvent.h>
#include <Weave/Profiles/device-control/DeviceControl.h>
#include <Weave/Profiles/service-directory/ServiceDirectory.h>
#pragma GCC diagnostic pop

namespace weavestack {

/// Handler for all fuchsia.weave/Stack FIDL protocol calls. Registers as a
/// public service with the ComponentContext and handles incoming connections.
class StackImpl : public fuchsia::weave::Stack {
 public:
  /// Construct a new instance of |StackImpl|.
  ///
  /// This method does not take ownership of the |context|.
  explicit StackImpl(sys::ComponentContext* context);
  virtual ~StackImpl();

  /// Initialize and register this instance as FIDL handler.
  zx_status_t Init();

  /// Get a |PairingStateWatcher| to get or watch for changes in pairing state.
  void GetPairingStateWatcher(
      fidl::InterfaceRequest<fuchsia::weave::PairingStateWatcher> watcher) override;
  /// Get a |ServiceDirectoryWatcher| watching an endpoint ID.
  void GetSvcDirectoryWatcher(
      uint64_t endpoint_id,
      fidl::InterfaceRequest<fuchsia::weave::SvcDirectoryWatcher> watcher) override;
  /// Retrieve a QR code that can be used in the pairing process.
  void GetQrCode(GetQrCodeCallback callback) override;
  /// Reset the Weave configuration.
  void ResetConfig(fuchsia::weave::ResetConfigFlags flags, ResetConfigCallback callback) override;

  /// Notify all active |PairingStateWatcher|s.
  void NotifyPairingState();
  /// Notify all active |SvcDirectoryWatcher|s.
  void NotifySvcDirectory();

 private:
  class PairingStateWatcherImpl;
  class SvcDirectoryWatcherImpl;

  // Access to device control server (overridable for testing).
  virtual nl::Weave::Profiles::DeviceControl::DeviceControlDelegate& GetDeviceControl();
  // Service directory lookup (overridable for testing).
  virtual zx_status_t LookupHostPorts(uint64_t endpoint_id,
                                      std::vector<fuchsia::weave::HostPort>* host_ports);

  // Device layer event handling.
  void HandleWeaveDeviceEvent(const nl::Weave::DeviceLayer::WeaveDeviceEvent* event);
  // Static handler to trampoline event calls into an instance, as the event
  // handler registration can only accept raw function pointers. The |arg|
  // argument is a pointer to the instance.
  static void TrampolineEvent(const nl::Weave::DeviceLayer::WeaveDeviceEvent* event, intptr_t arg);

  // Prevent copy/move construction
  StackImpl(const StackImpl&) = delete;
  StackImpl(StackImpl&&) = delete;
  // Prevent copy/move assignment
  StackImpl& operator=(const StackImpl&) = delete;
  StackImpl& operator=(StackImpl&&) = delete;

  // FIDL servicing related state
  fidl::BindingSet<fuchsia::weave::Stack> bindings_;
  fidl::BindingSet<fuchsia::weave::PairingStateWatcher, std::unique_ptr<PairingStateWatcherImpl>>
      pairing_state_watchers_;
  fidl::BindingSet<fuchsia::weave::SvcDirectoryWatcher, std::unique_ptr<SvcDirectoryWatcherImpl>>
      svc_directory_watchers_;
  sys::ComponentContext* context_;
  std::unique_ptr<fuchsia::weave::PairingState> last_pairing_state_;
};

}  // namespace weavestack

#endif  // SRC_CONNECTIVITY_WEAVE_WEAVESTACK_FIDL_STACK_IMPL_H_
