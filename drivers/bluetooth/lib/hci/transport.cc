// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "transport.h"

#include <lib/async/default.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>

#include "lib/fxl/logging.h"

#include "device_wrapper.h"

namespace btlib {
namespace hci {

// static
fxl::RefPtr<Transport> Transport::Create(
    std::unique_ptr<DeviceWrapper> hci_device) {
  return AdoptRef(new Transport(std::move(hci_device)));
}

Transport::Transport(std::unique_ptr<DeviceWrapper> hci_device)
    : hci_device_(std::move(hci_device)),
      is_initialized_(false),
      io_dispatcher_(nullptr),
      closed_cb_dispatcher_(nullptr) {
  FXL_DCHECK(hci_device_);
}

Transport::~Transport() {
  // Do nothing. Since Transport is shared across threads, this can be called
  // from any thread and calling ShutDown() would be unsafe.
}

bool Transport::Initialize(async_dispatcher_t* dispatcher) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(hci_device_);
  FXL_DCHECK(!command_channel_);
  FXL_DCHECK(!acl_data_channel_);
  FXL_DCHECK(!IsInitialized());

  // Obtain command channel handle.
  zx::channel channel = hci_device_->GetCommandChannel();
  if (!channel.is_valid()) {
    FXL_LOG(ERROR) << "hci: Transport: Failed to obtain command channel handle";
    return false;
  }

  if (dispatcher) {
    io_dispatcher_ = dispatcher;
  } else {
    io_loop_ = std::make_unique<async::Loop>();
    io_loop_->StartThread("hci-transport-io");
    io_dispatcher_ = io_loop_->dispatcher();
  }

  // We watch for handle errors and closures to perform the necessary clean up.
  WatchChannelClosed(channel, cmd_channel_wait_);
  command_channel_ = std::make_unique<CommandChannel>(this, std::move(channel));
  command_channel_->Initialize();

  is_initialized_ = true;

  return true;
}

bool Transport::InitializeACLDataChannel(
    const DataBufferInfo& bredr_buffer_info,
    const DataBufferInfo& le_buffer_info) {
  FXL_DCHECK(hci_device_);
  FXL_DCHECK(IsInitialized());

  // Obtain ACL data channel handle.
  zx::channel channel = hci_device_->GetACLDataChannel();
  if (!channel.is_valid()) {
    FXL_LOG(ERROR)
        << "hci: Transport: Failed to obtain ACL data channel handle";
    return false;
  }

  // We watch for handle errors and closures to perform the necessary clean up.
  WatchChannelClosed(channel, acl_channel_wait_);

  acl_data_channel_ =
      std::make_unique<ACLDataChannel>(this, std::move(channel));
  acl_data_channel_->Initialize(bredr_buffer_info, le_buffer_info);

  return true;
}

void Transport::SetTransportClosedCallback(
    fit::closure callback,
    async_dispatcher_t* dispatcher) {
  FXL_DCHECK(callback);
  FXL_DCHECK(dispatcher);
  FXL_DCHECK(!closed_cb_);
  FXL_DCHECK(!closed_cb_dispatcher_);

  closed_cb_ = std::move(callback);
  closed_cb_dispatcher_ = dispatcher;
}

void Transport::ShutDown() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(IsInitialized());

  FXL_LOG(INFO) << "hci: Transport: shutting down";

  if (acl_data_channel_) {
    acl_data_channel_->ShutDown();
  }
  if (command_channel_) {
    command_channel_->ShutDown();
  }

  async::PostTask(io_dispatcher_, [this] {
    cmd_channel_wait_.Cancel();
    if (acl_data_channel_) {
      acl_channel_wait_.Cancel();
    }
    if (io_loop_) {
      io_loop_->Quit();
    }
  });

  if (io_loop_) {
    io_loop_->JoinThreads();
  }

  // We avoid deallocating the channels here as they *could* still be accessed
  // by other threads. It's OK to clear |io_dispatcher_| as the channels hold
  // their own references to it.
  //
  // Once |io_loop_| joins above, |io_dispatcher_| may be defunct. However,
  // the channels are allowed to keep posting tasks on it (which will never
  // execute).

  io_dispatcher_ = nullptr;

  is_initialized_ = false;

  FXL_LOG(INFO) << "hci: Transport I/O loop exited";
}

bool Transport::IsInitialized() const {
  return is_initialized_;
}

void Transport::WatchChannelClosed(const zx::channel& channel,
                                   Waiter& wait) {
  async::PostTask(io_dispatcher_,
    [handle = channel.get(), &wait, this, ref = fxl::Ref(this)] {
    wait.set_object(handle);
    wait.set_trigger(ZX_CHANNEL_PEER_CLOSED);
    zx_status_t status = wait.Begin(async_get_default_dispatcher());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "hci: Transport: failed channel setup: "
                     << zx_status_get_string(status);
      wait.set_object(ZX_HANDLE_INVALID);
    }
  });
}

void Transport::OnChannelClosed(
    async_dispatcher_t* dispatcher,
    async::WaitBase* wait,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "hci: Transport: channel error: "
                   << zx_status_get_string(status);
  } else {
    FXL_DCHECK(signal->observed & ZX_CHANNEL_PEER_CLOSED);
  }

  NotifyClosedCallback();
}

void Transport::NotifyClosedCallback() {
  // Clear the handlers so that we stop receiving events.
  cmd_channel_wait_.Cancel();
  if (acl_data_channel_) {
    acl_channel_wait_.Cancel();
  }

  FXL_LOG(INFO) << "hci: Transport: HCI channel(s) were closed";
  if (closed_cb_)
    async::PostTask(closed_cb_dispatcher_, closed_cb_.share());
}

}  // namespace hci
}  // namespace btlib
