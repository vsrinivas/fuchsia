// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include <queue>

#include <zircon/compiler.h>

#include "apps/bluetooth/lib/common/cancelable_callback.h"
#include "apps/bluetooth/lib/l2cap/sdu.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/synchronization/thread_checker.h"
#include "lib/fxl/tasks/task_runner.h"

namespace bluetooth {
namespace l2cap {

// Represents a L2CAP channel. Each instance is owned by a service implementation that operates on
// the corresponding channel. Instances are created by and associated with a LogicalLink.
//
// A Channel can operate in one of 6 L2CAP Modes of Operation (see Core Spec v5.0, Vol 3, Part A,
// Section 2.4). Only Basic Mode is currently supported.
//
// USAGE:
//
// Channel is an abstract base class. There are two concrete implementations:
//
//   * internal::ChannelImpl (defined below) which implements a real L2CAP channel. Instances are
//     obtained from ChannelManager and tied to internal::LogicalLink instances.
//
//   * FakeChannel, which can be used for unit testing service-layer entities that operate on one or
//     more L2CAP channel(s).
//     TODO(armansito): Introduce FakeChannel later.
//
// THREAD-SAFETY:
//
// This class is thread-safe with the following caveats:
//
//   * Creation and deletion must always happen on the creation thread of the L2CAP ChannelManager.
//
//   * RxCallback will be accessed and frequently copied on the HCI I/O thread. Callers should take
//     care while managing the life time of objects that are referenced by the callback.
class Channel {
 public:
  virtual ~Channel() = default;

  ChannelId id() const { return id_; }

  // Sends the given SDU payload over this channel. This takes ownership of |sdu|. Returns false if
  // the SDU is rejected, for example because it exceeds the channel's MTU or because the link has
  // been closed.
  virtual bool Send(std::unique_ptr<const common::ByteBuffer> sdu) = 0;

  // Callback invoked when this channel has been closed without an explicit request from the owner
  // of this instance. For example, this can happen when the remote end closes a dynamically
  // configured channel or when the underlying logical link is terminated through other means.
  //
  // This callback is always run on this Channel's creation thread.
  using ClosedCallback = fxl::Closure;
  void set_channel_closed_callback(const ClosedCallback& callback) { closed_cb_ = callback; }

  // Callback invoked when a new SDU is received on this channel. Any previously buffered SDUs will
  // be sent to |rx_cb| right away, provided that |rx_cb| is not empty and the underlying logical
  // link is active.
  //
  // Setting |rx_cb| to empty will unregister the handler, but SDUs may still be delivered to the
  // old handler until this takes effect.
  //
  // If a non-empty |rx_cb| is provided, then the value of |rx_task_runner| must
  // not be nullptr. If |rx_cb| is empty (e.g. to clear the rx handler), then |rx_task_runner| must
  // be nullptr.
  //
  // See additional notes on thread safety above.
  using RxCallback = std::function<void(const SDU& sdu)>;
  virtual void SetRxHandler(const RxCallback& rx_cb,
                            fxl::RefPtr<fxl::TaskRunner> rx_task_runner) = 0;

 protected:
  explicit Channel(ChannelId id);

  bool IsCreationThreadCurrent() const { return thread_checker_.IsCreationThreadCurrent(); }

  const ClosedCallback& closed_callback() const { return closed_cb_; }

 private:
  ChannelId id_;
  ClosedCallback closed_cb_;

  fxl::ThreadChecker thread_checker_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Channel);
};

namespace internal {

class LogicalLink;

// Channel implementation used in production.
class ChannelImpl : public Channel {
 public:
  ~ChannelImpl() override;

  // Channel overrides:
  bool Send(std::unique_ptr<const common::ByteBuffer> sdu) override;
  void SetRxHandler(const RxCallback& rx_cb, fxl::RefPtr<fxl::TaskRunner> rx_task_runner) override;

 private:
  friend class internal::LogicalLink;

  // Only a LogicalLink can construct a ChannelImpl.
  ChannelImpl(ChannelId id, internal::LogicalLink* link);

  // Called by |link_| to notify us when the channel can no longer process data. This MUST NOT call
  // any locking methods of |link_| as that WILL cause a deadlock.
  void OnLinkClosed();

  // Called by |link_| when a PDU targeting this channel has been received. Contents of |pdu| will
  // be moved.
  void HandleRxPdu(PDU&& pdu);

  // The maximum SDU sizes for this channel.
  uint16_t tx_mtu_;
  uint16_t rx_mtu_;

  // TODO(armansito): Add MPS fields when we supported segmentation/flow-control.

  std::mutex mtx_;

  RxCallback rx_cb_ __TA_GUARDED(mtx_);
  fxl::RefPtr<fxl::TaskRunner> rx_task_runner_ __TA_GUARDED(mtx_);

  // The LogicalLink that this channel is associated with. A channel is always created by a
  // LogicalLink.
  //
  // |link_| is guaranteed to be valid as long as the link is active. When a LogicalLink is torn
  // down, it will notify all of its associated channels by calling OnLinkClosed() which sets
  // |link_| to nullptr.
  internal::LogicalLink* link_ __TA_GUARDED(mtx_);  // weak

  // The pending SDUs on this channel. Received PDUs are buffered if |rx_cb_| is currently not set.
  // TODO(armansito): We should avoid STL containers for data packets as they all implicitly
  // allocate. This is a reminder to fix this elsewhere (especially in the HCI layer).
  std::queue<SDU, std::list<SDU>> pending_rx_sdus_ __TA_GUARDED(mtx_);

  // We process all outgoing SDUs on the HCI I/O thread by posting a cancelable task.
  // NOTE: This cannot be protected using |mtx_| as that can lead to deadlock if a vended callback
  // attempts to acquire it. If access to this object needs to be protected, then it is better to
  // use a different mutex for it.
  common::CancelableCallbackFactory<void()> send_sdu_task_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ChannelImpl);
};

}  // namespace internal
}  // namespace l2cap
}  // namespace bluetooth
