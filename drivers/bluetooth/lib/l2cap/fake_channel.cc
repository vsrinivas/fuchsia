// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_channel.h"

#include "lib/fxl/functional/make_copyable.h"

namespace btlib {
namespace l2cap {
namespace testing {

FakeChannel::FakeChannel(ChannelId id,
                         hci::ConnectionHandle handle,
                         hci::Connection::LinkType link_type)
    : Channel(id, link_type),
      fragmenter_(handle),
      activate_fails_(false),
      link_error_(false),
      weak_ptr_factory_(this) {}

void FakeChannel::Receive(const common::ByteBuffer& data) {
  FXL_DCHECK(rx_cb_ && task_runner_);

  auto pdu = fragmenter_.BuildBasicFrame(id(), data);
  task_runner_->PostTask(
      fxl::MakeCopyable([cb = rx_cb_, pdu = std::move(pdu)] { cb(pdu); }));
}

void FakeChannel::SetSendCallback(const SendCallback& callback,
                                  fxl::RefPtr<fxl::TaskRunner> task_runner) {
  FXL_DCHECK(static_cast<bool>(callback) == static_cast<bool>(task_runner));

  send_cb_ = callback;
  send_task_runner_ = task_runner;
}

void FakeChannel::SetLinkErrorCallback(
    L2CAP::LinkErrorCallback callback,
    fxl::RefPtr<fxl::TaskRunner> task_runner) {
  FXL_DCHECK(static_cast<bool>(callback) == static_cast<bool>(task_runner));

  link_err_cb_ = std::move(callback);
  link_err_runner_ = task_runner;
}

void FakeChannel::Close() {
  if (closed_cb_)
    closed_cb_();
}

bool FakeChannel::Activate(RxCallback rx_callback,
                           ClosedCallback closed_callback,
                           fxl::RefPtr<fxl::TaskRunner> task_runner) {
  FXL_DCHECK(rx_callback);
  FXL_DCHECK(closed_callback);
  FXL_DCHECK(task_runner);
  FXL_DCHECK(!task_runner_);

  if (activate_fails_)
    return false;

  task_runner_ = task_runner;
  closed_cb_ = closed_callback;
  rx_cb_ = rx_callback;

  return true;
}

void FakeChannel::Deactivate() {
  task_runner_ = nullptr;
  closed_cb_ = {};
  rx_cb_ = {};
}

void FakeChannel::SignalLinkError() {
  link_error_ = true;

  if (link_err_cb_) {
    link_err_runner_->PostTask(link_err_cb_);
  }
}

bool FakeChannel::Send(std::unique_ptr<const common::ByteBuffer> sdu) {
  if (!send_cb_)
    return false;

  FXL_DCHECK(sdu);
  FXL_DCHECK(send_task_runner_);

  send_task_runner_->PostTask(fxl::MakeCopyable(
      [cb = send_cb_, sdu = std::move(sdu)]() mutable { cb(std::move(sdu)); }));

  return true;
}

}  // namespace testing
}  // namespace l2cap
}  // namespace btlib
