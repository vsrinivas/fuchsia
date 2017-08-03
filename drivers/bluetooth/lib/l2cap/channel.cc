// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "channel.h"

#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

#include "logical_link.h"

namespace bluetooth {
namespace l2cap {

Channel::Channel(ChannelId id) : id_(id) {
  FTL_DCHECK(id_);
}

namespace internal {

ChannelImpl::ChannelImpl(ChannelId id, internal::LogicalLink* link) : Channel(id), link_(link) {
  FTL_DCHECK(link_);
}

ChannelImpl::~ChannelImpl() {
  FTL_DCHECK(IsCreationThreadCurrent());

  std::lock_guard<std::mutex> lock(mtx_);

  if (link_) link_->RemoveChannel(this);
}

void ChannelImpl::SendBasicFrame(const common::ByteBuffer& payload) {
  // TODO(armansito): Implement
}

void ChannelImpl::SetRxHandler(const RxCallback& rx_cb,
                               ftl::RefPtr<ftl::TaskRunner> rx_task_runner) {
  FTL_DCHECK(IsCreationThreadCurrent());
  FTL_DCHECK(static_cast<bool>(rx_cb) == static_cast<bool>(rx_task_runner));

  std::lock_guard<std::mutex> lock(mtx_);

  // TODO(armansito): Consider wrapping |rx_cb| around a common::CancelableCallback to make it
  // cancelable when a new |rx_cb| is set. This would guarantee that a previously set callback will
  // never run after this method returns and the packet carried by the canceled |rx_cb| would be
  // dropped.
  rx_cb_ = rx_cb;
  rx_task_runner_ = rx_task_runner;

  if (link_ && rx_cb_) {
    while (!pending_sdus_.empty()) {
      auto cb =
          ftl::MakeCopyable([ cb = rx_cb_, sdu = std::move(pending_sdus_.front()) ] { cb(sdu); });
      pending_sdus_.pop();
      rx_task_runner_->PostTask(cb);
    }
  }
}

void ChannelImpl::OnLinkClosed() {
  FTL_DCHECK(IsCreationThreadCurrent());

  ClosedCallback cb;

  {
    std::lock_guard<std::mutex> lock(mtx_);

    FTL_DCHECK(link_);
    link_ = nullptr;

    // Drop any previously buffered SDUs.
    pending_sdus_ = {};

    if (!closed_callback()) return;

    // We'll invoke the callback synchronously. We copy the callback here and invoke it outside of
    // this block to prevent a potential deadlock.
    cb = closed_callback();
  }

  cb();
}

void ChannelImpl::HandleRxPdu(PDU&& pdu) {
  // Data is always received on a different thread.
  FTL_DCHECK(!IsCreationThreadCurrent());

  // TODO(armansito): This is the point where the channel mode implementation should take over the
  // PDU. Since we only support basic mode: SDU == PDU.

  std::lock_guard<std::mutex> lock(mtx_);
  FTL_DCHECK(link_);

  if (!rx_cb_) {
    pending_sdus_.emplace(std::forward<PDU>(pdu));
    return;
  }

  FTL_DCHECK(rx_task_runner_);
  rx_task_runner_->PostTask(ftl::MakeCopyable([ cb = rx_cb_, pdu = std::move(pdu) ] { cb(pdu); }));
}

}  // namespace internal
}  // namespace l2cap
}  // namespace bluetooth
