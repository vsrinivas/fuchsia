// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "channel.h"

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "logical_link.h"

namespace btlib {
namespace l2cap {

Channel::Channel(ChannelId id, hci::Connection::LinkType link_type)
    : id_(id),
      link_type_(link_type),

      // TODO(armansito): IWBN if the MTUs could be specified dynamically
      // instead (see NET-308).
      tx_mtu_(kDefaultMTU),
      rx_mtu_(kDefaultMTU) {
  FXL_DCHECK(id_);
  FXL_DCHECK(link_type_ == hci::Connection::LinkType::kLE ||
             link_type_ == hci::Connection::LinkType::kACL);
}

namespace internal {

ChannelImpl::ChannelImpl(ChannelId id,
                         fxl::WeakPtr<internal::LogicalLink> link,
                         std::list<PDU> buffered_pdus)
    : Channel(id, link->type()),
      dispatcher_(nullptr),
      link_(link),
      pending_rx_sdus_(std::move(buffered_pdus)) {
  FXL_DCHECK(link_);
}

bool ChannelImpl::Activate(RxCallback rx_callback,
                           ClosedCallback closed_callback,
                           async_t* dispatcher) {
  FXL_DCHECK(rx_callback);
  FXL_DCHECK(closed_callback);
  FXL_DCHECK(dispatcher);

  std::lock_guard<std::mutex> lock(mtx_);

  // Activating on a closed link has no effect. We also clear this on
  // deactivation to prevent a channel from being activated more than once.
  if (!link_)
    return false;

  FXL_DCHECK(!dispatcher_);
  dispatcher_ = dispatcher;
  rx_cb_ = std::move(rx_callback);
  closed_cb_ = std::move(closed_callback);

  // Route the buffered packets.
  if (!pending_rx_sdus_.empty()) {
    async::PostTask(
        dispatcher_,
        [func = rx_cb_, pending = std::move(pending_rx_sdus_)]() mutable {
          while (!pending.empty()) {
            func(std::move(pending.front()));
            pending.pop();
          }
        });

    FXL_DCHECK(pending_rx_sdus_.empty());
  }

  return true;
}

void ChannelImpl::Deactivate() {
  std::lock_guard<std::mutex> lock(mtx_);

  // De-activating on a closed link has no effect.
  if (!link_ || !dispatcher_) {
    link_.reset();
    return;
  }

  FXL_DCHECK(dispatcher_);
  dispatcher_ = nullptr;
  rx_cb_ = {};
  closed_cb_ = {};

  // Tell the link to release this channel on its thread.
  async::PostTask(link_->dispatcher(), [this, link = link_, id = id()] {
    // If |link| is still alive than |this| must be valid since |link| holds a
    // reference to us.
    if (link) {
      link->RemoveChannel(this);
    }
  });

  link_.reset();
}

void ChannelImpl::SignalLinkError() {
  std::lock_guard<std::mutex> lock(mtx_);

  // Cannot signal an error on a closed or deactivated link.
  if (!link_ || !dispatcher_)
    return;

  async::PostTask(link_->dispatcher(), [link = link_] { link->SignalError(); });
}

bool ChannelImpl::Send(std::unique_ptr<const common::ByteBuffer> sdu) {
  FXL_DCHECK(sdu);

  if (sdu->size() > tx_mtu()) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "l2cap: SDU size exceeds channel TxMTU (channel-id: 0x%04x)", id());
    return false;
  }

  std::lock_guard<std::mutex> lock(mtx_);

  if (!link_) {
    FXL_LOG(ERROR) << "l2cap: Cannot send SDU on a closed link";
    return false;
  }

  // Drop the packet if the channel is inactive.
  if (!dispatcher_)
    return false;

  async::PostTask(link_->dispatcher(),
                  [id = id(), link = link_, sdu = std::move(sdu)] {
                    if (link) {
                      link->SendBasicFrame(id, *sdu);
                    }
                  });

  return true;
}

void ChannelImpl::OnLinkClosed() {
  std::lock_guard<std::mutex> lock(mtx_);

  if (!link_ || !dispatcher_) {
    link_.reset();
    return;
  }

  FXL_DCHECK(closed_cb_);

  async::PostTask(dispatcher_, std::move(closed_cb_));
  dispatcher_ = nullptr;
}

void ChannelImpl::HandleRxPdu(PDU&& pdu) {
  // TODO(armansito): This is the point where the channel mode implementation
  // should take over the PDU. Since we only support basic mode: SDU == PDU.

  std::lock_guard<std::mutex> lock(mtx_);

  // This will only be called on a live link.
  FXL_DCHECK(link_);

  // Buffer the packets if the channel hasn't been activated.
  if (!dispatcher_) {
    pending_rx_sdus_.emplace(std::forward<PDU>(pdu));
    return;
  }

  FXL_DCHECK(rx_cb_);
  async::PostTask(dispatcher_,
                  [func = rx_cb_, pdu = std::move(pdu)] { func(pdu); });
}

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
