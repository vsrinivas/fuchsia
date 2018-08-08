// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "host.h"

#include "garnet/drivers/bluetooth/lib/hci/device_wrapper.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"

#include "fidl/host_server.h"
#include "gatt_host.h"

using namespace btlib;

namespace bthost {

Host::Host(const bt_hci_protocol_t& hci_proto) : hci_proto_(hci_proto) {}

Host::~Host() {}

bool Host::Initialize(InitCallback callback) {
  auto dev = std::make_unique<hci::DdkDeviceWrapper>(hci_proto_);
  auto hci = hci::Transport::Create(std::move(dev));
  if (!hci)
    return false;

  FXL_VLOG(1) << "bt-host: initializing HCI";
  if (!hci->Initialize()) {
    FXL_LOG(ERROR) << "bt-host: failed to initialize HCI transport";
    return false;
  }

  l2cap_ = l2cap::L2CAP::Create(hci, "bt-host (l2cap)");
  if (!l2cap_)
    return false;

  gatt_host_ = GattHost::Create("bt-host (gatt)");
  if (!gatt_host_)
    return false;

  gap_ = std::make_unique<gap::Adapter>(hci, l2cap_, gatt_host_->profile());
  if (!gap_)
    return false;

  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(l2cap_);

  // Called when the GAP layer is ready. We initialize L2CAP and the GATT
  // profile after initial setup in GAP (which sets up ACL data).
  auto gap_init_callback = [l2cap = l2cap_, gatt_host = gatt_host_,
                            callback = std::move(callback)](bool success) {
    FXL_VLOG(1) << "bt-host: GAP initialized";

    if (success) {
      l2cap->Initialize();
      gatt_host->Initialize();
    }

    callback(success);
  };

  FXL_VLOG(1) << "bt-host: initializing GAP";
  return gap_->Initialize(std::move(gap_init_callback), [] {
    FXL_VLOG(1) << "bt-host: HCI transport has closed";
  });
}

void Host::ShutDown() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_VLOG(1) << "bt-host: shutting down";

  // Closes all FIDL channels owned by |host_server_|.
  host_server_ = nullptr;

  // This shuts down the GATT profile and L2CAP and all of their clients.
  gatt_host_->ShutDown();
  l2cap_->ShutDown();

  // Make sure that |gap_| gets shut down and destroyed on its creation thread
  // as it is not thread-safe.
  gap_ = nullptr;
}

void Host::BindHostInterface(zx::channel channel) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  if (host_server_) {
    FXL_LOG(WARNING) << "bt-host: Host interface channel already open!";
    return;
  }

  FXL_DCHECK(gap_);
  FXL_DCHECK(gatt_host_);

  host_server_ = std::make_unique<HostServer>(std::move(channel),
                                              gap_->AsWeakPtr(), gatt_host_);
  host_server_->set_error_handler([this] {
    FXL_DCHECK(host_server_);
    FXL_VLOG(1) << "bt-host: Host interface disconnected";
    host_server_ = nullptr;
  });
}

}  // namespace bthost
