// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "transport.h"

#include <magenta/device/bt-hci.h>
#include <magenta/status.h>
#include <magenta/types.h>
#include <mx/channel.h>

#include "lib/ftl/logging.h"
#include "lib/mtl/threading/create_thread.h"

namespace bluetooth {
namespace hci {

Transport::Transport(ftl::UniqueFD device_fd)
    : device_fd_(std::move(device_fd)),
      is_running_(false),
      cmd_channel_handler_key_(0u),
      acl_channel_handler_key_(0u) {}

Transport::~Transport() {
  if (is_running_) ShutDown();
}

bool Transport::Initialize() {
  FTL_DCHECK(device_fd_.get());
  FTL_DCHECK(!is_running_);

  // Obtain command channel handle.
  mx_handle_t handle = MX_HANDLE_INVALID;
  ssize_t ioctl_status = ioctl_bt_hci_get_command_channel(device_fd_.get(), &handle);
  if (ioctl_status < 0) {
    FTL_LOG(ERROR) << "hci: Failed to obtain command channel handle: "
                   << mx_status_get_string(ioctl_status);
    return false;
  }

  FTL_DCHECK(handle != MX_HANDLE_INVALID);
  is_running_ = true;
  io_thread_ = mtl::CreateThread(&io_task_runner_, "hci-transport");

  // We watch for handle errors and closures to perform the necessary clean up.
  io_task_runner_->PostTask([handle, this] {
    cmd_channel_handler_key_ =
        mtl::MessageLoop::GetCurrent()->AddHandler(this, handle, MX_CHANNEL_PEER_CLOSED);
  });

  mx::channel channel(handle);
  command_channel_ = std::make_unique<CommandChannel>(this, std::move(channel));
  command_channel_->Initialize();

  return true;
}

bool Transport::InitializeACLDataChannel(
    size_t max_data_len, size_t le_max_data_len, size_t max_num_packets, size_t le_max_num_packets,
    const ACLDataChannel::ConnectionLookupCallback& conn_lookup_cb,
    const ACLDataChannel::DataReceivedCallback& rx_callback,
    ftl::RefPtr<ftl::TaskRunner> rx_task_runner) {
  FTL_DCHECK(device_fd_.get());
  FTL_DCHECK(is_running_);

  // Obtain ACL data channel handle.
  mx_handle_t handle = MX_HANDLE_INVALID;
  ssize_t ioctl_status = ioctl_bt_hci_get_acl_data_channel(device_fd_.get(), &handle);
  if (ioctl_status < 0) {
    FTL_LOG(ERROR) << "hci: Failed to obtain ACL data channel handle: "
                   << mx_status_get_string(ioctl_status);
    return false;
  }

  // We watch for handle errors and closures to perform the necessary clean up.
  io_task_runner_->PostTask([handle, this] {
    acl_channel_handler_key_ =
        mtl::MessageLoop::GetCurrent()->AddHandler(this, handle, MX_CHANNEL_PEER_CLOSED);
  });

  mx::channel channel(handle);
  acl_data_channel_ = std::make_unique<ACLDataChannel>(this, std::move(channel), conn_lookup_cb,
                                                       rx_callback, rx_task_runner);
  acl_data_channel_->Initialize(max_data_len, le_max_data_len, max_num_packets, le_max_num_packets);

  return true;
}

void Transport::SetTransportClosedCallback(const ftl::Closure& callback,
                                           ftl::RefPtr<ftl::TaskRunner> task_runner) {
  FTL_DCHECK(callback);
  FTL_DCHECK(task_runner);
  FTL_DCHECK(!closed_cb_);
  FTL_DCHECK(!closed_cb_task_runner_);

  closed_cb_ = callback;
  closed_cb_task_runner_ = task_runner;
}

void Transport::InitializeForTesting(std::unique_ptr<CommandChannel> cmd_channel,
                                     std::unique_ptr<ACLDataChannel> acl_data_channel) {
  FTL_DCHECK(cmd_channel);
  FTL_DCHECK(!is_running_);

  is_running_ = true;
  io_thread_ = mtl::CreateThread(&io_task_runner_, "hci-transport-test");

  command_channel_ = std::move(cmd_channel);
  if (acl_data_channel) acl_data_channel_ = std::move(acl_data_channel);

  // We watch for handle errors and closures to perform the necessary clean up.
  io_task_runner_->PostTask([this] {
    cmd_channel_handler_key_ = mtl::MessageLoop::GetCurrent()->AddHandler(
        this, command_channel()->channel().get(), MX_CHANNEL_PEER_CLOSED);
    if (this->acl_data_channel()) {
      acl_channel_handler_key_ = mtl::MessageLoop::GetCurrent()->AddHandler(
          this, this->acl_data_channel()->channel().get(), MX_CHANNEL_PEER_CLOSED);
    }
  });
}

void Transport::ShutDown() {
  FTL_DCHECK(is_running_);

  FTL_LOG(INFO) << "hci: Transport: shutting down";

  if (acl_data_channel_) acl_data_channel_->ShutDown();
  if (command_channel_) command_channel_->ShutDown();

  io_task_runner_->PostTask(
      [ cmd_key = cmd_channel_handler_key_, acl_key = acl_channel_handler_key_ ] {
        FTL_DCHECK(mtl::MessageLoop::GetCurrent());
        mtl::MessageLoop::GetCurrent()->RemoveHandler(cmd_key);
        mtl::MessageLoop::GetCurrent()->RemoveHandler(acl_key);
        mtl::MessageLoop::GetCurrent()->QuitNow();
      });

  if (io_thread_.joinable()) io_thread_.join();

  acl_data_channel_ = nullptr;
  command_channel_ = nullptr;
  is_running_ = false;
  io_task_runner_ = nullptr;

  FTL_LOG(INFO) << "hci: Transport I/O loop exited";
}

void Transport::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  FTL_DCHECK(pending & MX_CHANNEL_PEER_CLOSED);
  NotifyClosedCallback();
}

void Transport::OnHandleError(mx_handle_t handle, mx_status_t error) {
  FTL_LOG(ERROR) << "hci: Transport: channel error: " << mx_status_get_string(error);
  NotifyClosedCallback();
}

void Transport::NotifyClosedCallback() {
  FTL_DCHECK(io_task_runner_->RunsTasksOnCurrentThread());

  // Clear the handlers so that we stop receiving events.
  mtl::MessageLoop::GetCurrent()->RemoveHandler(cmd_channel_handler_key_);
  mtl::MessageLoop::GetCurrent()->RemoveHandler(acl_channel_handler_key_);

  FTL_LOG(INFO) << "hci: Transport: HCI channel(s) were closed";
  if (closed_cb_) closed_cb_task_runner_->PostTask(closed_cb_);
}

}  // namespace hci
}  // namespace bluetooth
