// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fastboot_tcp.h"

#include <lib/fit/result.h>
#include <stdio.h>

#include "fastboot.h"
#include "gigaboot/src/inet6.h"
#include "gigaboot/src/tcp.h"
#include "phys/efi/main.h"
#include "zircon_boot_ops.h"

namespace gigaboot {
namespace {
constexpr uint16_t kFbServerPort = 5554;

zx::status<> TcpInitialize(tcp6_socket &fb_tcp_socket) {
  if (fb_tcp_socket.binding_protocol) {
    return zx::ok();
  }

  static_assert(sizeof(efi_ipv6_addr) == sizeof(ll_ip6_addr), "IP6 address size mismatch");
  efi_ipv6_addr efi_ll_addr;
  memcpy(&efi_ll_addr, &ll_ip6_addr, sizeof(ll_ip6_addr));

  while (true) {
    if (tcp6_open(&fb_tcp_socket, gEfiSystemTable->BootServices, &efi_ll_addr, kFbServerPort) ==
        TCP6_RESULT_SUCCESS) {
      printf("Fastboot TCP is ready\n");
      return zx::ok();
    }
  }
}

// Statically allocate the download buffer
constexpr size_t kDownloadBufferSize = 512 * 1024 * 1024;
uint8_t download_buffer[kDownloadBufferSize];

}  // namespace

class TcpTransport : public TcpTransportInterface {
 public:
  TcpTransport(tcp6_socket &fb_tcp_socket) : fb_tcp_socket_(fb_tcp_socket) {}

  bool Read(void *out, size_t size) override {
    tcp6_result res = TCP6_RESULT_PENDING;
    ZX_ASSERT(size < UINT32_MAX);
    // Block until complete or error.
    while (res == TCP6_RESULT_PENDING) {
      res = tcp6_read(&fb_tcp_socket_, out, static_cast<uint32_t>(size));
    }
    return res == TCP6_RESULT_SUCCESS;
  }

  bool Write(const void *data, size_t size) override {
    tcp6_result res = TCP6_RESULT_PENDING;
    ZX_ASSERT(size < UINT32_MAX);
    // Block until complete or error.
    while (res == TCP6_RESULT_PENDING) {
      res = tcp6_write(&fb_tcp_socket_, data, static_cast<uint32_t>(size));
    }
    return res == TCP6_RESULT_SUCCESS;
  }

 private:
  tcp6_socket &fb_tcp_socket_;
};

zx::status<> FastbootTcpMain() {
  tcp6_socket fb_tcp_socket = {};
  auto init_res = TcpInitialize(fb_tcp_socket);
  if (init_res.is_error()) {
    return zx::error(ZX_ERR_INTERNAL);
  }

  TcpTransport transport(fb_tcp_socket);
  ZirconBootOps zb_ops = gigaboot::GetZirconBootOps();
  Fastboot fastboot(download_buffer, zb_ops);

  while (true) {
    // TODO(b/235489025): Investigate broadcasting mdns here.

    tcp6_result result = tcp6_accept(&fb_tcp_socket);

    if (result == TCP6_RESULT_SUCCESS) {
      printf("Receive client connection\n");
      FastbootTcpSession(transport, fastboot);
      if (fastboot.IsContinue()) {
        // Since we'll be handing over to OS, the closing will be treated as
        // best errort only.
        tcp6_close(&fb_tcp_socket);
        return zx::ok();
      }

      printf("Disconnecting tcp6...");
      auto disconnect_res = tcp6_disconnect(&fb_tcp_socket);
      while (disconnect_res == TCP6_RESULT_PENDING) {
        disconnect_res = tcp6_disconnect(&fb_tcp_socket);
      }

      if (disconnect_res != TCP6_RESULT_SUCCESS) {
        printf("Failed to disconnect socket, %d\n", disconnect_res);
        return zx::error(ZX_ERR_INTERNAL);
      }
      printf("disconnected\n");
    }
  }
}

}  // namespace gigaboot
