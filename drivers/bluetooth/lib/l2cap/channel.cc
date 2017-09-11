// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "channel.h"

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "logical_link.h"

namespace bluetooth {
namespace l2cap {

Channel::Channel(ChannelId id) : id_(id) {
  FXL_DCHECK(id_);
}

namespace internal {

ChannelImpl::ChannelImpl(ChannelId id, internal::LogicalLink* link)
    : Channel(id), tx_mtu_(kDefaultMTU), rx_mtu_(kDefaultMTU), link_(link) {
  FXL_DCHECK(link_);
}

ChannelImpl::~ChannelImpl() {
  FXL_DCHECK(IsCreationThreadCurrent());

  // Cancel all unprocessed SDU tasks before acquiring |mtx_|.
  send_sdu_task_factory_.CancelAll();

  std::lock_guard<std::mutex> lock(mtx_);

  if (link_) link_->RemoveChannel(this);
}

bool ChannelImpl::Send(std::unique_ptr<const common::ByteBuffer> sdu) {
  FXL_DCHECK(sdu);

  if (sdu->size() > tx_mtu_) {
    FXL_VLOG(1) << fxl::StringPrintf("l2cap: SDU size exceeds channel TxMTU (channel-id: 0x%04x)",
                                     id());
    return false;
  }

  std::lock_guard<std::mutex> lock(mtx_);

  if (!link_) {
    FXL_LOG(ERROR) << "l2cap: Cannot send SDU on a closed link";
    return false;
  }

  link_->io_task_runner()->PostTask(
      send_sdu_task_factory_.MakeTask(fxl::MakeCopyable([ this, sdu = std::move(sdu) ] {
        std::lock_guard<std::mutex> lock(mtx_);

        // Check if the link was closed in the mean time.
        if (!link_) {
          FXL_LOG(ERROR) << "l2cap: Cannot send SDU on a closed link";
          return;
        }

        // TODO(armansito): Since we only support Basic Mode we send the SDU out right away. This is
        // the point where a channel mode implementation should take over.

        link_->SendBasicFrame(id(), *sdu);
      })));

  return true;
}

void ChannelImpl::SetRxHandler(const RxCallback& rx_cb,
                               fxl::RefPtr<fxl::TaskRunner> rx_task_runner) {
  FXL_DCHECK(IsCreationThreadCurrent());
  FXL_DCHECK(static_cast<bool>(rx_cb) == static_cast<bool>(rx_task_runner));

  std::lock_guard<std::mutex> lock(mtx_);

  // TODO(armansito): Consider wrapping |rx_cb| around a common::CancelableCallback to make it
  // cancelable when a new |rx_cb| is set. This would guarantee that a previously set callback will
  // never run after this method returns and the packet carried by the canceled |rx_cb| would be
  // dropped.
  rx_cb_ = rx_cb;
  rx_task_runner_ = rx_task_runner;

  if (link_ && rx_cb_) {
    while (!pending_rx_sdus_.empty()) {
      auto cb = fxl::MakeCopyable(
          [ cb = rx_cb_, sdu = std::move(pending_rx_sdus_.front()) ] { cb(sdu); });
      pending_rx_sdus_.pop();
      rx_task_runner_->PostTask(cb);
    }
  }
}

void ChannelImpl::OnLinkClosed() {
  FXL_DCHECK(IsCreationThreadCurrent());

  ClosedCallback cb;

  // Cancel all unprocessed SDU tasks before acquiring |mtx_|.
  send_sdu_task_factory_.CancelAll();

  {
    std::lock_guard<std::mutex> lock(mtx_);

    FXL_DCHECK(link_);
    link_ = nullptr;

    // Drop any previously buffered SDUs.
    pending_rx_sdus_ = {};

    if (!closed_callback()) return;

    // We'll invoke the callback synchronously. We copy the callback here and invoke it outside of
    // this block to prevent a potential deadlock.
    cb = closed_callback();
  }

  cb();
}

void ChannelImpl::HandleRxPdu(PDU&& pdu) {
  // Data is always received on the HCI I/O thread which is assumed to be different from this
  // Channel's creation thread.
  FXL_DCHECK(!IsCreationThreadCurrent());

  // TODO(armansito): This is the point where the channel mode implementation should take over the
  // PDU. Since we only support basic mode: SDU == PDU.

  std::lock_guard<std::mutex> lock(mtx_);
  FXL_DCHECK(link_);
  FXL_DCHECK(link_->io_task_runner()->RunsTasksOnCurrentThread());

  if (!rx_cb_) {
    pending_rx_sdus_.emplace(std::forward<PDU>(pdu));
    return;
  }

  FXL_DCHECK(rx_task_runner_);
  rx_task_runner_->PostTask(fxl::MakeCopyable([ cb = rx_cb_, pdu = std::move(pdu) ] { cb(pdu); }));
}

}  // namespace internal
}  // namespace l2cap
}  // namespace bluetooth
