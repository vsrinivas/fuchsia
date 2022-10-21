// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "guest_ethernet.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>

#include "guest_ethernet_interface.h"

GuestEthernet::~GuestEthernet() {
  loop_.Shutdown();
  loop_.JoinThreads();
}

zx_status_t GuestEthernet::StartDispatchLoop() {
  return loop_.StartThread("guest-ethernet-dispatcher");
}

zx_status_t GuestEthernet::Initialize(const void* rust_guest_ethernet, const uint8_t* mac,
                                      size_t mac_len, bool enable_bridge) {
  set_status_ = [rust_guest_ethernet](zx_status_t status) {
    guest_ethernet_set_status(rust_guest_ethernet, status);
  };
  ready_for_guest_tx_ = [rust_guest_ethernet]() {
    guest_ethernet_ready_for_tx(rust_guest_ethernet);
  };
  send_guest_rx_ = [rust_guest_ethernet](const uint8_t* data, size_t len, uint32_t buffer_id) {
    guest_ethernet_receive_rx(rust_guest_ethernet, data, len, buffer_id);
  };

  if (mac_len != VIRTIO_ETH_MAC_SIZE) {
    FX_LOGS(ERROR) << "Virtio-net device received an incorrectly sized MAC address. Expected "
                   << VIRTIO_ETH_MAC_SIZE << ", got " << mac_len << ".";
    return ZX_ERR_INVALID_ARGS;
  }
  std::memcpy(mac_address_, mac, VIRTIO_ETH_MAC_SIZE);

  // Initialize is running on the Rust main thread, but CreateGuestInterface must be run on the
  // C++ dispatcher thread. The Rust device will wait for this task to complete.
  async::PostTask(loop_.dispatcher(), [this, enable_bridge]() {
    this->set_status_(this->CreateGuestInterface(enable_bridge));
  });

  return ZX_OK;
}

zx_status_t GuestEthernet::CreateGuestInterface(bool enable_bridge) {
  // See fxbug.dev/101224 for NAT support.
  if (!enable_bridge) {
    FX_LOGS(ERROR) << "Only bridging is currently supported";
    return ZX_ERR_NOT_SUPPORTED;
  }

  // TODO(fxbug.dev/95485): Do this.
  return ZX_OK;
}

zx_status_t GuestEthernet::Send(void* data, uint16_t length) {
  // TODO(fxbug.dev/95485): Do this.
  return ZX_OK;
}

void GuestEthernet::Complete(uint32_t buffer_id, zx_status_t status) {
  // TODO(fxbug.dev/95485): Do this.
}
