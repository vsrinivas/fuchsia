// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ethertap_client.h"
#include <errno.h>
#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>
#include <lib/zx/socket.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/ethertap.h>
#include <zircon/status.h>
#include <zircon/types.h>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>

static const char kTapctl[] = "/dev/misc/tapctl";

typedef struct ethertap_data_in {
  ethertap_socket_header_t header;
  uint8_t data[];
} ethertap_data_in_t;

namespace netemul {

class EthertapClientImpl : public EthertapClient {
 public:
  explicit EthertapClientImpl(zx::socket sock, EthertapConfig config)
      : buf_(config.mtu + sizeof(ethertap_socket_header_t)),
        config_(std::move(config)),
        sock_(std::move(sock)) {
    sock_data_wait_.set_object(sock_.get());
    sock_data_wait_.set_trigger(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED);

    WaitOnSocket();
  }

  void SetLinkUp(bool linkUp) override {
    sock_.signal_peer(
        0, linkUp ? ETHERTAP_SIGNAL_ONLINE : ETHERTAP_SIGNAL_OFFLINE);
  }

  zx_status_t Send(const void* data, size_t len, size_t* sent) override {
    return sock_.write(0u, data, len, sent);
  }

  void SetPacketCallback(PacketCallback cb) override {
    packet_callback_ = std::move(cb);
  }

  void SetPeerClosedCallback(PeerClosedCallback cb) override {
    peer_closed_callback_ = std::move(cb);
  }

  static std::unique_ptr<EthertapClientImpl> Create(
      const EthertapConfig& incfg) {
    zx::socket sock;
    fbl::unique_fd ctlfd(open(kTapctl, O_RDONLY));
    if (!ctlfd.is_valid()) {
      fprintf(stderr, "could not open %s: %s\n", kTapctl, strerror(errno));
      return nullptr;
    }

    ethertap_ioctl_config_t config = {};
    strlcpy(config.name, incfg.name.c_str(), ETHERTAP_MAX_NAME_LEN);
    config.options = incfg.options;
    config.mtu = incfg.mtu;
    memcpy(config.mac, incfg.mac.d, 6);
    ssize_t rc = ioctl_ethertap_config(ctlfd.get(), &config,
                                       sock.reset_and_get_address());
    if (rc < 0) {
      auto status = static_cast<zx_status_t>(rc);
      fprintf(stderr, "could not configure ethertap device: %s\n",
              zx_status_get_string(status));
      return nullptr;
    }

    return std::make_unique<EthertapClientImpl>(std::move(sock), incfg);
  }

  void Close() override {
    sock_data_wait_.Cancel();
    sock_.reset();
  }

  const zx::socket& socket() override { return sock_; }

  void OnSocketSignal(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                      zx_status_t status, const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
      fprintf(stderr, "Ethertap OnSocketSignal bad status %s\n",
              zx_status_get_string(status));
      return;
    }

    if (signal->observed & ZX_SOCKET_READABLE) {
      size_t read;

      status = sock_.read(0u, buf_.data(), buf_.size(), &read);
      if (status != ZX_OK) {
        fprintf(stderr, "Ethertap OnSocketSignal read failed %s\n",
                zx_status_get_string(status));
        return;
      }
      if (read < sizeof(ethertap_socket_header_t)) {
        fprintf(
            stderr,
            "Ethertap socket read too short, expecting at least %ld header, "
            "got %ld\n",
            sizeof(ethertap_socket_header_t), read);
      }

      auto d = reinterpret_cast<ethertap_data_in_t*>(buf_.data());
      read -= sizeof(ethertap_socket_header_t);

      if (d->header.type == ETHERTAP_MSG_PACKET && packet_callback_) {
        packet_callback_(d->data, read);
      }
    }

    if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
      fprintf(stderr, "Ethertap OnSocketSignal peer closed\n");
      if (peer_closed_callback_) {
        peer_closed_callback_();
      }
    } else {
      WaitOnSocket();
    }
  }

  void WaitOnSocket() {
    zx_status_t status = sock_data_wait_.Begin(async_get_default_dispatcher());
    if (status != ZX_OK) {
      fprintf(stderr, "Can't wait on ethertap socket: %s\n",
              zx_status_get_string(status));
    }
  }

  std::vector<uint8_t> buf_;
  EthertapConfig config_;
  zx::socket sock_;
  PacketCallback packet_callback_;
  PeerClosedCallback peer_closed_callback_;
  async::WaitMethod<EthertapClientImpl, &EthertapClientImpl::OnSocketSignal>
      sock_data_wait_{this};
};

std::unique_ptr<EthertapClient> EthertapClient::Create(
    const EthertapConfig& config) {
  return EthertapClientImpl::Create(config);
}

}  // namespace netemul