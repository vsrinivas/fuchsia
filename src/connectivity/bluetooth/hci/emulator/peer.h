// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_EMULATOR_PEER_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_EMULATOR_PEER_H_

#include <fuchsia/bluetooth/test/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/result.h>

#include <optional>
#include <vector>

#include <fbl/macros.h>

#include "src/connectivity/bluetooth/core/bt-host/fidl/helpers.h"
#include "src/connectivity/bluetooth/core/bt-host/sdp/service_record.h"
#include "src/connectivity/bluetooth/core/bt-host/testing/fake_controller.h"
#include "src/connectivity/bluetooth/core/bt-host/testing/fake_peer.h"
#include "src/connectivity/bluetooth/lib/fidl/hanging_getter.h"

namespace bt_hci_emulator {

// Responsible for processing FIDL messages to/from an emulated peer instance. This class is not
// thread-safe.
//
// When the remote end of the FIDL channel gets closed the underlying FakePeer will be removed from
// the fake controller and the |closed_callback| that is passed to the constructor will get
// notified. The owner of this object should act on this by destroying this Peer instance.
class Peer : public fuchsia::bluetooth::test::Peer {
 public:
  using Result = fit::result<std::unique_ptr<Peer>, fuchsia::bluetooth::test::EmulatorPeerError>;

  // Registers a peer with the FakeController using the provided LE parameters. Returns the peer on
  // success or an error reporting the failure.
  static Result NewLowEnergy(fuchsia::bluetooth::test::LowEnergyPeerParameters parameters,
                             fidl::InterfaceRequest<fuchsia::bluetooth::test::Peer> request,
                             fbl::RefPtr<bt::testing::FakeController> fake_controller);

  // Registers a peer with the FakeController using the provided BR/EDR parameters. Returns the peer
  // on success or an error reporting the failure.
  static Result NewBredr(fuchsia::bluetooth::test::BredrPeerParameters parameters,
                         fidl::InterfaceRequest<fuchsia::bluetooth::test::Peer> request,
                         fbl::RefPtr<bt::testing::FakeController> fake_controller);

  // The destructor unregisters the Peer if initialized.
  ~Peer();

  // Rerturns the device address that this instance was initialized with based on the FIDL
  // parameters.
  const bt::DeviceAddress& address() const { return address_; }

  // Assign a callback that will run when the Peer handle gets closed.
  void set_closed_callback(fit::callback<void()> closed_callback) {
    closed_callback_ = std::move(closed_callback);
  }

  // fuchsia::bluetooth::test::Peer overrides:
  void AssignConnectionStatus(fuchsia::bluetooth::test::HciError status,
                              AssignConnectionStatusCallback callback) override;
  void EmulateLeConnectionComplete(fuchsia::bluetooth::ConnectionRole role) override;
  void EmulateDisconnectionComplete() override;
  void WatchConnectionStates(WatchConnectionStatesCallback callback) override;

  // Updates this peer with the current connection state which is used to notify its FIDL client
  // of state changes that it is observing.
  void UpdateConnectionState(bool connected);

 private:
  Peer(bt::DeviceAddress address, fidl::InterfaceRequest<fuchsia::bluetooth::test::Peer> request,
       fbl::RefPtr<bt::testing::FakeController> fake_controller);

  void OnChannelClosed(zx_status_t status);
  void CleanUp();
  void NotifyChannelClosed();

  bt::DeviceAddress address_;
  fbl::RefPtr<bt::testing::FakeController> fake_controller_;
  fidl::Binding<fuchsia::bluetooth::test::Peer> binding_;
  fit::callback<void()> closed_callback_;

  bt_lib_fidl::HangingVectorGetter<fuchsia::bluetooth::test::ConnectionState>
      connection_state_getter_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(Peer);
};

}  // namespace bt_hci_emulator

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_EMULATOR_PEER_H_
