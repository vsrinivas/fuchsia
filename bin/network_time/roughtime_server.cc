// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/network_time/roughtime_server.h"

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <string>

#include <client.h>
#include <openssl/rand.h>

#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/syslog/cpp/logger.h"

namespace time_zone {

bool RoughTimeServer::IsValid() const { return valid_; }

Status RoughTimeServer::GetTimeFromServer(
    roughtime::rough_time_t* timestamp) const {
  if (!IsValid()) {
    FX_LOGS(ERROR) << "Time server not supported: " << address_;
    return NOT_SUPPORTED;
  }
  // Create Socket
  const size_t colon_offset = address_.rfind(':');
  if (colon_offset == std::string::npos) {
    FX_LOGS(ERROR) << "No port number in server address: " << address_;
    return NOT_SUPPORTED;
  }

  std::string host(address_.substr(0, colon_offset));
  const std::string port_str(address_.substr(colon_offset + 1));

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;
  hints.ai_flags = AI_NUMERICSERV;

  if (!host.empty() && host[0] == '[' && host[host.size() - 1] == ']') {
    host = host.substr(1, host.size() - 1);
    hints.ai_family = AF_INET6;
    hints.ai_flags |= AI_NUMERICHOST;
  }

  struct addrinfo* addrs;
  int err = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &addrs);
  if (err != 0) {
    FX_LOGS(ERROR) << "Failed to resolve " << address_ << ": "
                   << gai_strerror(err);
    return NETWORK_ERROR;
  }
  auto ac1 = fxl::MakeAutoCall([&]() { freeaddrinfo(addrs); });
  fxl::UniqueFD sock_ufd(
      socket(addrs->ai_family, addrs->ai_socktype, addrs->ai_protocol));
  if (!sock_ufd.is_valid()) {
    FX_LOGS(ERROR) << "Failed to create UDP socket: " << strerror(errno);
    return NETWORK_ERROR;
  }
  int sock_fd = sock_ufd.get();

  if (connect(sock_fd, addrs->ai_addr, addrs->ai_addrlen)) {
    FX_LOGS(ERROR) << "Failed to connect UDP socket: " << strerror(errno);
    return NETWORK_ERROR;
  }

  char dest_str[INET6_ADDRSTRLEN];
  err = getnameinfo(addrs->ai_addr, addrs->ai_addrlen, dest_str,
                    sizeof(dest_str), NULL, 0, NI_NUMERICHOST);

  if (err != 0) {
    FX_LOGS(ERROR) << "getnameinfo: " << gai_strerror(err);
    return NETWORK_ERROR;
  }

  FX_VLOGS(1) << "Sending request to " << dest_str << ", port " << port_str;

  uint8_t nonce[roughtime::kNonceLength];
  RAND_bytes(nonce, sizeof(nonce));
  const std::string request = roughtime::CreateRequest(nonce);

  int timeout = 3 * 1000;  // in milliseconds

  ssize_t r;
  do {
    r = send(sock_fd, request.data(), request.size(), 0);
  } while (r == -1 && errno == EINTR);
  const uint64_t start_us = zx_clock_get(ZX_CLOCK_MONOTONIC);

  if (r < 0 || static_cast<size_t>(r) != request.size()) {
    FX_LOGS(ERROR) << "send on UDP socket" << strerror(errno);
    return NETWORK_ERROR;
  }

  uint8_t recv_buf[roughtime::kMinRequestSize];
  ssize_t buf_len;
  pollfd readfd;
  readfd.fd = sock_fd;
  readfd.events = POLLIN;
  fd_set readfds;
  FD_SET(sock_fd, &readfds);
  int ret = poll(&readfd, 1, timeout);
  if (ret < 0) {
    FX_LOGS(ERROR) << "poll on UDP socket: " << strerror(errno);
    return NETWORK_ERROR;
  }
  if (ret == 0) {
    FX_LOGS(ERROR) << "timeout while poll";
    return NETWORK_ERROR;
  }
  if (readfd.revents != POLLIN) {
    FX_LOGS(ERROR) << "Error poll, revents = " << readfd.revents;
    return NETWORK_ERROR;
  }
  buf_len = recv(sock_fd, recv_buf, sizeof(recv_buf), 0 /* flags */);

  const uint64_t end_us = zx_clock_get(ZX_CLOCK_MONOTONIC);

  if (buf_len == -1) {
    FX_LOGS(ERROR) << "recv from UDP socket: " << strerror(errno);
    return NETWORK_ERROR;
  }

  uint32_t radius;
  std::string error;
  if (!roughtime::ParseResponse(timestamp, &radius, &error, public_key_,
                                recv_buf, buf_len, nonce)) {
    FX_LOGS(ERROR) << "Response from " << address_ << " failed verification: ",
        error;
    return BAD_RESPONSE;
  }

  *timestamp += (end_us - start_us) / 2;
  return OK;
}

}  // namespace time_zone
