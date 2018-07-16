// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_CHANNEL_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_CHANNEL_H_

#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include <queue>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <zircon/compiler.h>

#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/l2cap/sdu.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/synchronization/thread_checker.h"

namespace btlib {
namespace l2cap {

// Represents a L2CAP channel. Each instance is owned by a service
// implementation that operates on the corresponding channel. Instances can only
// be obtained from a ChannelManager.
//
// A Channel can operate in one of 6 L2CAP Modes of Operation (see Core Spec
// v5.0, Vol 3, Part A, Section 2.4). Only Basic Mode is currently supported.
//
// USAGE:
//
// Channel is an abstract base class. There are two concrete implementations:
//
//   * internal::ChannelImpl (defined below) which implements a real L2CAP
//     channel. Instances are obtained from ChannelManager and tied to
//     internal::LogicalLink instances.
//
//   * FakeChannel, which can be used for unit testing service-layer entities
//     that operate on one or more L2CAP channel(s).
//
// Production instances are obtained from a ChannelManager. Channels are safe to
// activate, deactivate, and destroy on any thread.
//
// A Channel's owner must explicitly call Deactivate() and must not rely on
// dropping its reference to close the channel.
//
// When a LogicalLink closes, All of its  active channels become deactivated
// when it closes and this is signaled by running the ClosedCallback passed to
// Activate().
class Channel : public fbl::RefCounted<Channel> {
 public:
  // Identifier for this channel's endpoint on this device. It can be prior-
  // specified for fixed channels or allocated for dynamic channels per v5.0,
  // Vol 3, Part A, Section 2.1 "Channel Identifiers." Channels on a link will
  // have unique identifiers to each other.
  ChannelId id() const { return id_; }

  // Identifier for this channel's endpoint on the remote peer. Same value as
  // |id()| for fixed channels and allocated by the remote for dynamic channels.
  ChannelId remote_id() const { return remote_id_; }

  // TODO(xow): Remove setters after fixing tests to no longer mutate Channels
  void set_id_for_testing(ChannelId id) { id_ = id; }
  void set_remote_id_for_testing(ChannelId id) { remote_id_ = id; }

  // Callback invoked when this channel has been closed without an explicit
  // request from the owner of this instance. For example, this can happen when
  // the remote end closes a dynamically configured channel or when the
  // underlying logical link is terminated through other means.
  using ClosedCallback = fit::closure;

  // Callback invoked when a new SDU is received on this channel. Any previously
  // buffered SDUs will be sent to |rx_cb| right away, provided that |rx_cb| is
  // not empty and the underlying logical link is active.
  using RxCallback = fit::function<void(SDU sdu)>;

  // Activates this channel assigning |dispatcher| to execute |rx_callback| and
  // |closed_callback|.
  //
  // Returns false if the channel's link has been closed.
  //
  // NOTE: Callers shouldn't assume that this method will succeed, as the
  // underlying link can be removed at any time.
  virtual bool Activate(RxCallback rx_callback, ClosedCallback closed_callback,
                        async_dispatcher_t* dispatcher) = 0;

  // Deactivates this channel. No more packets can be sent or received after
  // this is called. |rx_callback| may still be called if it has been already
  // dispatched to its task runner.
  //
  // This method is idempotent.
  virtual void Deactivate() = 0;

  // Signals that the underlying link should be disconnected. This should be
  // called when a service layer protocol error requires the connection to be
  // severed.
  //
  // The link error callback (provided to L2CAP::Register* methods) is invoked
  // as a result of this operation. The handler is responsible for actually
  // disconnecting the link.
  //
  // This does not deactivate the channel, though the channel is expected to
  // close when the link gets removed later.
  virtual void SignalLinkError() = 0;

  // Sends the given SDU payload over this channel. This takes ownership of
  // |sdu|. Returns false if the SDU is rejected, for example because it exceeds
  // the channel's MTU or because the link has been closed.
  virtual bool Send(std::unique_ptr<const common::ByteBuffer> sdu) = 0;

  // The type of the logical link this channel operates on.
  hci::Connection::LinkType link_type() const { return link_type_; }

  // The connection handle of the underlying logical link.
  hci::ConnectionHandle link_handle() const { return link_handle_; }

  uint16_t tx_mtu() const { return tx_mtu_; }
  uint16_t rx_mtu() const { return rx_mtu_; }

 protected:
  friend class fbl::RefPtr<Channel>;
  Channel(ChannelId id, ChannelId remote_id,
          hci::Connection::LinkType link_type,
          hci::ConnectionHandle link_handle);
  virtual ~Channel() = default;

  ChannelId id_;
  ChannelId remote_id_;
  hci::Connection::LinkType link_type_;
  hci::ConnectionHandle link_handle_;

  // The maximum SDU sizes for this channel.
  uint16_t tx_mtu_;
  uint16_t rx_mtu_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Channel);
};

namespace internal {

class LogicalLink;

// Channel implementation used in production.
class ChannelImpl : public Channel {
 public:
  // Channel overrides:
  bool Activate(RxCallback rx_callback, ClosedCallback closed_callback,
                async_dispatcher_t* dispatcher) override;
  void Deactivate() override;
  void SignalLinkError() override;
  bool Send(std::unique_ptr<const common::ByteBuffer> sdu) override;

 private:
  friend class fbl::RefPtr<ChannelImpl>;
  friend class internal::LogicalLink;

  ChannelImpl(ChannelId id, ChannelId remote_id,
              fxl::WeakPtr<internal::LogicalLink> link,
              std::list<PDU> buffered_pdus);
  ~ChannelImpl() override = default;

  // Called by |link_| to notify us when the channel can no longer process data.
  // This MUST NOT call any locking methods of |link_| as that WILL cause a
  // deadlock.
  void OnLinkClosed();

  // Called by |link_| when a PDU targeting this channel has been received.
  // Contents of |pdu| will be moved.
  void HandleRxPdu(PDU&& pdu);

  // TODO(armansito): Add MPS fields when we support segmentation/flow-control.

  std::mutex mtx_;

  bool active_ __TA_GUARDED(mtx_);
  async_dispatcher_t* dispatcher_ __TA_GUARDED(mtx_);
  RxCallback rx_cb_ __TA_GUARDED(mtx_);
  ClosedCallback closed_cb_ __TA_GUARDED(mtx_);

  // The LogicalLink that this channel is associated with. A channel is always
  // created by a LogicalLink.
  //
  // |link_| is guaranteed to be valid as long as the link is active. This is
  // because when a LogicalLink is torn down, it will notify all of its
  // associated channels by calling OnLinkClosed() which sets |link_| to
  // nullptr.
  //
  // The WeakPtr itself does not guarantee validity if used on any thread other
  // than the LogicalLink's creation thread.
  fxl::WeakPtr<internal::LogicalLink> link_ __TA_GUARDED(mtx_);

  // The pending SDUs on this channel. Received PDUs are buffered if |rx_cb_| is
  // currently not set.
  // TODO(armansito): We should avoid STL containers for data packets as they
  // all implicitly allocate. This is a reminder to fix this elsewhere
  // (especially in the HCI layer).
  std::queue<SDU, std::list<SDU>> pending_rx_sdus_ __TA_GUARDED(mtx_);

  FXL_DISALLOW_COPY_AND_ASSIGN(ChannelImpl);
};

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_CHANNEL_H_
