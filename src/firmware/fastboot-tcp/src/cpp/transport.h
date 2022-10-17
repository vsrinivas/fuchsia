// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_FASTBOOT_TCP_SRC_CPP_TRANSPORT_H_
#define SRC_FIRMWARE_FASTBOOT_TCP_SRC_CPP_TRANSPORT_H_

#include <lib/fastboot/fastboot.h>

class FastbootTCPTransport : public fastboot::Transport {
 public:
  FastbootTCPTransport(void *ctx, size_t input_packet_size,
                       int (*receive_packet_callback)(void *, size_t, void *),
                       int (*send_packet)(const void *, size_t, void *))
      : ctx_(ctx),
        input_packet_size_(input_packet_size),
        read_packet_callback_(receive_packet_callback),
        write_packet_callback_(send_packet) {}

  ~FastbootTCPTransport() = default;

  size_t PeekPacketSize() override { return input_packet_size_; }

  zx::result<size_t> ReceivePacket(void *dst, size_t capacity) override {
    if (!dst) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }

    if (capacity < PeekPacketSize()) {
      return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
    }

    if (read_packet_callback_(dst, PeekPacketSize(), ctx_)) {
      return zx::error(ZX_ERR_INTERNAL);
    }

    return zx::ok(PeekPacketSize());
  }

  zx::result<> Send(std::string_view packet) override {
    if (write_packet_callback_(packet.data(), packet.size(), ctx_)) {
      return zx::error(ZX_ERR_INTERNAL);
    }

    return zx::ok();
  }

 private:
  void *ctx_;
  size_t input_packet_size_;
  int (*read_packet_callback_)(void *, size_t, void *);
  int (*write_packet_callback_)(const void *, size_t, void *);
};

#endif  // SRC_FIRMWARE_FASTBOOT_TCP_SRC_CPP_TRANSPORT_H_
