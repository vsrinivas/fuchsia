// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_NET_RS_SRC_CPP_GUEST_ETHERNET_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_NET_RS_SRC_CPP_GUEST_ETHERNET_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/trace-provider/provider.h>
#include <zircon/types.h>

#include <virtio/net.h>

class GuestEthernet {
 public:
  GuestEthernet()
      : loop_(&kAsyncLoopConfigAttachToCurrentThread),
        trace_provider_(loop_.dispatcher()),
        svc_(sys::ServiceDirectory::CreateFromNamespace()) {}
  ~GuestEthernet();

  // Starts the dispatch loop on a new thread.
  zx_status_t StartDispatchLoop();

  // Initializes this guest ethernet object by parsing the Rust provided MAC address, preparing
  // callbacks, and registering it the netstack. This will be invoked by the Rust thread, and
  // scheduled on the C++ dispatch loop.
  //
  // Returns ZX_OK if it was successfully scheduled, and sends ZX_OK via set_status_ when finished.
  zx_status_t Initialize(const void* rust_guest_ethernet, const uint8_t* mac, size_t mac_len,
                         bool enable_bridge);

  // Send the packet to the netstack, returning ZX_OK if the packet was sent successfully, and
  // ZX_ERR_SHOULD_WAIT if no buffer space is available and the device should retry later.
  zx_status_t Send(void* data, uint16_t length);

  // Indicate that a packet has been successfully sent to the guest and that the memory can be
  // reclaimed.
  void Complete(uint32_t buffer_id, zx_status_t status);

 private:
  // Register this guest ethernet object with the netstack.
  zx_status_t CreateGuestInterface(bool enable_bridge);

  async::Loop loop_;
  trace::TraceProviderWithFdio trace_provider_;
  std::shared_ptr<sys::ServiceDirectory> svc_;

  uint8_t mac_address_[VIRTIO_ETH_MAC_SIZE];

  fit::function<void()> ready_for_guest_tx_;
  fit::function<void(zx_status_t)> set_status_;
  fit::function<void(uint8_t*, size_t, uint32_t)> send_guest_rx_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_NET_RS_SRC_CPP_GUEST_ETHERNET_H_
