// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_L2CAP_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_L2CAP_H_

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "garnet/drivers/bluetooth/lib/l2cap/le_signaling_channel.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace l2cap {

class Channel;

// This is the root object of the L2CAP layer. It owns the internal blocks of
// L2CAP, including:
//
//   * The ChannelManager which handles the ACL > Link > Channel routing and
//     signaling;
//
//   * Waiting on L2CAP sockets;
//
// A production L2CAP (obtained via L2CAP::Create()) spawns a thread with a
// dispatcher which is used to serially dispatch all internal L2CAP tasks.
//
// L2CAP is defined as a pure-virtual interface, so that a fake can be injected
// while testing layers that depend on it.
//
// L2CAP is thread-safe. All public functions are asynchronous and can be called
// from any thread.
class L2CAP : public fbl::RefCounted<L2CAP> {
 public:
  using ChannelCallback = fit::function<void(fbl::RefPtr<Channel>)>;
  using LEConnectionParameterUpdateCallback =
      internal::LESignalingChannel::ConnectionParameterUpdateCallback;
  using LinkErrorCallback = fit::closure;

  // Constructs an uninitialized L2CAP object that can be used in production.
  // This spawns a thread on which L2CAP tasks will be scheduled (using
  // |thread_name| as the name).
  static fbl::RefPtr<L2CAP> Create(fxl::RefPtr<hci::Transport> hci,
                                   std::string thread_name);

  // These send a Initialize/Shutdown message to the L2CAP task runner. It is
  // safe for the caller to drop its reference after ShutDown is called.
  //
  // Operations on an uninitialized/shut-down L2CAP have no effect.
  virtual void Initialize() = 0;
  virtual void ShutDown() = 0;

  // Registers an ACL connection with the L2CAP layer. L2CAP channels can be
  // opened on the logical link represented by |handle| after a call to this
  // method.
  //
  // |link_error_callback| will be used to notify when a channel signals a link
  // error. It will be posted onto |dispatcher|.
  //
  // Has no effect if L2CAP is uninitialized or shut down.
  virtual void AddACLConnection(hci::ConnectionHandle handle,
                                hci::Connection::Role role,
                                LinkErrorCallback link_error_callback,
                                async_dispatcher_t* dispatcher) = 0;

  // Registers an LE connection with the L2CAP layer. L2CAP channels can be
  // opened on the logical link represented by |handle| after a call to this
  // method.
  //
  // |conn_param_callback| will be used to notify the caller if new connection
  // parameters were accepted from the remote end of the link.
  //
  // |link_error_callback| will be used to notify when a channel signals a link
  // error.
  //
  // Upon successful registration of the link, |channel_callback| will be called
  // with the ATT and SMP fixed channels.
  //
  // Has no effect if L2CAP is uninitialized or shut down.
  using AddLEConnectionCallback =
      fit::function<void(fbl::RefPtr<Channel> att, fbl::RefPtr<Channel> smp)>;
  virtual void AddLEConnection(
      hci::ConnectionHandle handle, hci::Connection::Role role,
      LEConnectionParameterUpdateCallback conn_param_callback,
      LinkErrorCallback link_error_callback,
      AddLEConnectionCallback channel_callback,
      async_dispatcher_t* dispatcher) = 0;

  // Removes a previously registered connection. All corresponding Channels will
  // be closed and all incoming data packets on this link will be dropped.
  //
  // NOTE: It is recommended that a link entry be removed AFTER the controller
  // sends a HCI Disconnection Complete Event for the corresponding logical
  // link. This is to prevent incorrectly buffering data if the controller has
  // more packets to send after removing the link entry.
  //
  // Has no effect if L2CAP is uninitialized or shut down.
  virtual void RemoveConnection(hci::ConnectionHandle handle) = 0;

  // Registers a handler for peer-initiated dynamic channel requests that have
  // the Protocol/Service Multiplexing (PSM) code |psm|.
  //
  // |cb| will be called on |dispatcher| with the channel created by each
  // inbound connection request received. Handlers must be unregistered before
  // they are replaced.
  //
  // Returns false if |psm| is invalid or already has a handler registered.
  //
  // Inbound connection requests with a PSM that has no registered handler will
  // be rejected.
  //
  // Has no effect if L2CAP is uninitialized or shut down.
  //
  // TODO(xow): NET-1084 Pass in required channel configurations. Call signature
  //            will likely change.
  // TODO(xow): Dynamic PSMs may need their routing space (ACL or LE) identified
  virtual bool RegisterService(PSM psm, ChannelCallback cb,
                               async_dispatcher_t* dispatcher) = 0;

  // Removes the handler for inbound channel requests for the previously-
  // registered service identified by |psm|. This only prevents new inbound
  // channels from being opened but does not close already-open channels.
  //
  // Has no effect if L2CAP is uninitialized or shut down.
  virtual void UnregisterService(PSM psm) = 0;

 protected:
  friend class fbl::RefPtr<L2CAP>;
  L2CAP() = default;
  virtual ~L2CAP() = default;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(L2CAP);
};

}  // namespace l2cap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_L2CAP_H_
