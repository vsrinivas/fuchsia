// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "host.h"

#include "garnet/drivers/bluetooth/lib/hci/device_wrapper.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"

using namespace btlib;

namespace bthost {

Host::Host(const bt_hci_protocol_t& hci_proto) {
  auto dev = std::make_unique<hci::DdkDeviceWrapper>(hci_proto);
  auto hci = hci::Transport::Create(std::move(dev));

  adapter_ = std::make_unique<gap::Adapter>(hci);
  FXL_DCHECK(adapter_);
}

bool Host::Initialize(InitCallback callback) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  return adapter_->Initialize(callback, [] {
    FXL_VLOG(1) << "bthost: adapter closed";

    // TODO(armansito): Report this to HostDevice so it can remove itself?
  });
}

void Host::ShutDown() {
  adapter_->ShutDown();
}

}  // namespace bthost
