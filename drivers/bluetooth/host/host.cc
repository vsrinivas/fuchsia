// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "host.h"

#include "garnet/drivers/bluetooth/lib/hci/device_wrapper.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"

#include "lib/fsl/threading/create_thread.h"

#include "fidl/host_server.h"

using namespace btlib;

namespace bthost {

Host::Host(const bt_hci_protocol_t& hci_proto) {
  auto dev = std::make_unique<hci::DdkDeviceWrapper>(hci_proto);
  auto hci = hci::Transport::Create(std::move(dev));

  l2cap_ = l2cap::L2CAP::Create(hci, "bt-host (l2cap)");
  FXL_DCHECK(l2cap_);

  gap_ = std::make_unique<gap::Adapter>(hci, l2cap_);
  FXL_DCHECK(gap_);
}

Host::~Host() {}

bool Host::Initialize(InitCallback callback) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(l2cap_);

  auto gap_init_callback = [l2cap = l2cap_,
                            callback = std::move(callback)](bool success) {
    FXL_VLOG(1) << "bt-host: GAP initialized";

    if (success) {
      // Set up the L2CAP system. This must be done after |hci_| is initialized
      // as L2CAP needs to interact with the ACL channel.
      l2cap->Initialize();
    }

    callback(success);
  };

  return gap_->Initialize(gap_init_callback, [] {
    FXL_VLOG(1) << "bt-host: HCI transport has closed";
  });
}

void Host::ShutDown() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_VLOG(1) << "bt-host: shutting down";

  // Closes all FIDL channels owned by |host_server_|.
  host_server_ = nullptr;

  gap_->ShutDown();
  l2cap_->ShutDown();
}

void Host::BindHostInterface(zx::channel channel) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  if (host_server_) {
    FXL_LOG(WARNING) << "bt-host: Host interface channel already open!";
    return;
  }

  FXL_DCHECK(gap_);
  host_server_ =
      std::make_unique<HostServer>(std::move(channel), gap_->AsWeakPtr());
  host_server_->set_error_handler([this] {
    FXL_DCHECK(host_server_);
    FXL_VLOG(1) << "bt-host: Host disconnected";
    host_server_ = nullptr;
  });
}

}  // namespace bthost
