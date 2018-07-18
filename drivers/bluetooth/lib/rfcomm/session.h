// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_RFCOMM_SESSION_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_RFCOMM_SESSION_H_

#include <queue>
#include <unordered_map>

#include <fbl/ref_ptr.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/l2cap/channel.h"
#include "garnet/drivers/bluetooth/lib/l2cap/scoped_channel.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/channel.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/frames.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/rfcomm.h"

namespace btlib {
namespace rfcomm {

class Session {
 public:
  void Send(DLCI dlci, common::ByteBufferPtr data);

 private:
  friend class ChannelManager;

  // Returns nullptr if creation fails -- for example, if activating the L2CAP
  // channel fails. |channel_opened_cb| will be called whenever a new channel is
  // opened on this session. The callback will be dispatched on |dispatcher|.
  // |dispatcher| will also be used for dispatching all of Session's other
  // tasks.
  using ChannelOpenedCallback =
      fit::function<void(fbl::RefPtr<Channel>, ServerChannel)>;
  static std::unique_ptr<Session> Create(
      fbl::RefPtr<l2cap::Channel> l2cap_channel,
      ChannelOpenedCallback channel_opened_cb, async_dispatcher_t* dispatcher);

  // Should only be called from Create().
  Session(ChannelOpenedCallback channel_opened_cb,
          async_dispatcher_t* dispatcher);

  // Sets |l2cap_channel| as the Session's underlying L2CAP channel.
  // |l2cap_channel| should not be activated. This function activates
  // |l2cap_channel|; returns true iff channel activation succeeds. Should only
  // be called from Create() during Session creation.
  bool SetL2CAPChannel(fbl::RefPtr<l2cap::Channel> l2cap_channel);

  // Opens a remote channel and delivers it via |channel_opened_cb|.
  void OpenRemoteChannel(ServerChannel server_channel,
                         ChannelOpenedCallback channel_opened_cb);

  // l2cap::Channel callbacks.
  void RxCallback(const l2cap::SDU& sdu);
  void ClosedCallback();

  // The raw frame-sending function. Some frames (e.g. SABM and DISC) expect a
  // response (UA or DM). This function takes an optional callback which should
  // be called when the response is received. If an error occurs, the callback
  // is not called. Otherwise, if |callback| is given, it will be called with a
  // valid UA or DM frame.
  using FrameResponseCallback = fit::function<void(std::unique_ptr<Frame>)>;
  bool SendFrame(std::unique_ptr<Frame> frame,
                 FrameResponseCallback callback = nullptr);

  // Handle an incoming SABM request.
  void HandleSABM(DLCI dlci);

  // Begin the multiplexer start-up routine described in RFCOMM 5.2.1. This
  // function implements the "initiator" side of the multiplexer startup
  // protocol. For the "responder" side, see HandleSABM().
  void StartupMultiplexer();

  // Sets |role_|, and runs any Tasks that were pending multiplexer startup.
  void SetMultiplexerStarted(Role role);

  inline bool multiplexer_started() { return IsMultiplexerStarted(role_); }

  void Closedown();

  l2cap::ScopedChannel l2cap_channel_;

  // The RFCOMM role of this device for this particular Session. This is
  // determined not when the object is created, but when the multiplexer control
  // channel is set up.
  Role role_;

  // Whether or not this Session is using credit-based flow, as described in the
  // RFCOMM spec. Whether credit-based flow is being used is determined in the
  // first Parameter Negotiation interaction.
  bool credit_based_flow_;

  // Keeps track of opened channels.
  std::unordered_map<DLCI, fxl::WeakPtr<l2cap::Channel>> channels_;

  // Called when the remote peer opens a new incoming channel. The session
  // object constructs a new channel and then passes ownership of the channel
  // via this callback.
  ChannelOpenedCallback channel_opened_cb_;

  // This dispatcher is used for all tasks, including the ChannelOpenCallback
  // passed in to Create().
  async_dispatcher_t* dispatcher_;

  // Tasks which are to be run once the multiplexer starts.
  std::queue<fit::closure> tasks_pending_mux_startup_;

  // Called when a command frame or a multiplexer command doesn't receive a
  // response.
  using TimeoutCallback = async::TaskClosure;

  // Outstanding frames awaiting responses. GSM 5.4.4.1 states that there can be
  // at most one command with the P bit set to 1 outstanding on a given DLC at
  // any time. Thus, we can identify outstanding frames by their DLCI.
  using FrameResponseCallbacks =
      std::pair<FrameResponseCallback, std::unique_ptr<TimeoutCallback>>;
  std::unordered_map<DLCI, FrameResponseCallbacks> outstanding_frames_;

  fxl::WeakPtrFactory<Session> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Session);
};

}  // namespace rfcomm
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_RFCOMM_SESSION_H_
