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

  std::thread l2cap_thread =
      fsl::CreateThread(&l2cap_runner_, "bt-host (l2cap)");
  FXL_DCHECK(l2cap_runner_);

  l2cap_ = l2cap::L2CAP::Create(hci, l2cap_runner_);
  FXL_DCHECK(l2cap_);

  adapter_ = std::make_unique<gap::Adapter>(hci, l2cap_);
  FXL_DCHECK(adapter_);

  l2cap_thread.detach();
}

Host::~Host() {
  ShutDown();
}

bool Host::Initialize(InitCallback callback) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  // This will also initialize the L2CAP layer.
  return adapter_->Initialize(callback, [] {
    FXL_VLOG(1) << "bthost: adapter closed";

    // TODO(armansito): Report this to HostDevice so it can remove itself?
  });
}

void Host::ShutDown() {
  // Closes all FIDL channels owned by |host_server_|.
  host_server_ = nullptr;

  // This also shuts down L2CAP (to mirror initialization).
  adapter_->ShutDown();

  l2cap_runner_->PostTask([] { fsl::MessageLoop::GetCurrent()->QuitNow(); });
}

void Host::BindHostInterface(zx::channel channel) {
  if (host_server_) {
    FXL_LOG(WARNING) << "host: Host interface channel already open!";
    return;
  }

  FXL_DCHECK(adapter_);
  host_server_ =
      std::make_unique<HostServer>(std::move(channel), adapter_->AsWeakPtr());
  host_server_->set_error_handler([this] {
    FXL_DCHECK(host_server_);
    FXL_VLOG(1) << "host: Host disconnected";
    host_server_ = nullptr;
  });
}

}  // namespace bthost
