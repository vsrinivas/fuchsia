// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "channel.h"

#include "lib/ftl/logging.h"

#include "logical_link.h"

namespace bluetooth {
namespace l2cap {

Channel::Channel(ChannelId id) : id_(id) {
  FTL_DCHECK(id_);
}

void Channel::SetRxHandler(const RxCallback& rx_cb, ftl::RefPtr<ftl::TaskRunner> rx_task_runner) {
  FTL_DCHECK(IsCreationThreadCurrent());
  FTL_DCHECK(static_cast<bool>(rx_cb) == static_cast<bool>(rx_task_runner));

  std::lock_guard<std::mutex> lock(mtx_);
  rx_cb_ = rx_cb;
  rx_task_runner_ = rx_task_runner;
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

void ChannelImpl::OnLinkClosed() {
  FTL_DCHECK(IsCreationThreadCurrent());

  ClosedCallback cb;

  {
    std::lock_guard<std::mutex> lock(mtx_);

    FTL_DCHECK(link_);
    link_ = nullptr;

    if (!closed_callback()) return;

    // We'll invoke the callback synchronously. We copy the callback here and invoke it outside of
    // this block to prevent a potential deadlock.
    cb = closed_callback();
  }

  cb();
}

}  // namespace internal
}  // namespace l2cap
}  // namespace bluetooth
