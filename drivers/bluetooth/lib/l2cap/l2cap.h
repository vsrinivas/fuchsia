// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

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
// A production L2CAP (obtained via L2CAP::Create()) spawns a thread with an
// async dispatcher which is used to serially dispatch all internal L2CAP tasks.
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
  using LinkErrorCallback = std::function<void()>;  // copyable

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
  virtual void RegisterACL(
      hci::ConnectionHandle handle,
      hci::Connection::Role role,
      LinkErrorCallback link_error_callback,
      async_t* dispatcher) = 0;

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
  // Has no effect if L2CAP is uninitialized or shut down.
  virtual void RegisterLE(
      hci::ConnectionHandle handle,
      hci::Connection::Role role,
      LEConnectionParameterUpdateCallback conn_param_callback,
      LinkErrorCallback link_error_callback,
      async_t* dispatcher) = 0;

  // Removes a previously registered connection. All corresponding Channels will
  // be closed and all incoming data packets on this link will be dropped.
  //
  // NOTE: It is recommended that a link entry be removed AFTER the controller
  // sends a HCI Disconnection Complete Event for the corresponding logical
  // link. This is to prevent incorrectly buffering data if the controller has
  // more packets to send after removing the link entry.
  //
  // Has no effect if L2CAP is uninitialized or shut down.
  virtual void Unregister(hci::ConnectionHandle handle) = 0;

  // Opens the L2CAP fixed channel with |channel_id| over the logical link
  // identified by |connection_handle| and starts routing packets.
  //
  // The resulting channel will be returned asynchronously via |callback| on the
  // requested |dispatcher|. Runs |callback| with nullptr if the channel is
  // already open.
  //
  // Has no effect if L2CAP is uninitialized or shut down. |callback| will not
  // run in this case.
  //
  // TODO(armansito): Replace this with a version that returns all fixed
  // channels to avoid jumping through an asynchronous callback for each
  // channel. Probably one for LE and one for Classic.
  virtual void OpenFixedChannel(hci::ConnectionHandle handle,
                                ChannelId id,
                                ChannelCallback callback,
                                async_t* dispatcher) = 0;

 protected:
  friend class fbl::RefPtr<L2CAP>;
  L2CAP() = default;
  virtual ~L2CAP() = default;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(L2CAP);
};

}  // namespace l2cap
}  // namespace btlib
