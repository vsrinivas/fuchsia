// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_channel.h"

#include "lib/fxl/functional/make_copyable.h"

namespace bluetooth {
namespace l2cap {
namespace testing {

FakeChannel::FakeChannel(ChannelId id, hci::ConnectionHandle handle)
    : Channel(id), fragmenter_(handle), weak_ptr_factory_(this) {}

void FakeChannel::Receive(const common::ByteBuffer& data) {
  FXL_DCHECK(rx_cb_ && rx_task_runner_);

  auto pdu = fragmenter_.BuildBasicFrame(id(), data);
  rx_task_runner_->PostTask(
      fxl::MakeCopyable([cb = rx_cb_, pdu = std::move(pdu)] { cb(pdu); }));
}

void FakeChannel::SetSendCallback(const SendCallback& callback,
                                  fxl::RefPtr<fxl::TaskRunner> task_runner) {
  FXL_DCHECK(static_cast<bool>(callback) == static_cast<bool>(task_runner));

  send_cb_ = callback;
  send_task_runner_ = task_runner;
}

void FakeChannel::Close() {
  if (closed_callback())
    closed_callback()();
}

bool FakeChannel::Send(std::unique_ptr<const common::ByteBuffer> sdu) {
  if (!send_cb_)
    return false;

  FXL_DCHECK(sdu);
  FXL_DCHECK(send_cb_);

  send_task_runner_->PostTask(fxl::MakeCopyable(
      [cb = send_cb_, sdu = std::move(sdu)]() mutable { cb(std::move(sdu)); }));

  return true;
}

void FakeChannel::SetRxHandler(const RxCallback& rx_cb,
                               fxl::RefPtr<fxl::TaskRunner> rx_task_runner) {
  FXL_DCHECK(static_cast<bool>(rx_cb) == static_cast<bool>(rx_task_runner));
  rx_cb_ = rx_cb;
  rx_task_runner_ = rx_task_runner;
}

}  // namespace testing
}  // namespace l2cap
}  // namespace bluetooth
