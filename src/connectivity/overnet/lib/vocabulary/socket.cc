// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/vocabulary/socket.h"
#include <unistd.h>
#include <cstring>
#include <sstream>

namespace overnet {

void Socket::Close() {
  if (socket_ != -1) {
    close(socket_);
    socket_ = -1;
  }
}

Status Socket::SetOptReusePort(bool reuse) {
  return SetOpt(SOL_SOCKET, SO_REUSEPORT, reuse ? 1 : 0)
      .WithContext(reuse ? "Enable SO_REUSEPORT" : "Disable SO_REUSEPORT");
}

Status Socket::SetOpt(int level, int opt, void* value, size_t value_size) {
  if (!IsValid()) {
    return Status(StatusCode::INVALID_ARGUMENT, "Invalid socket");
  }
  if (setsockopt(socket_, level, opt, value, value_size) < 0) {
    return Status(StatusCode::INVALID_ARGUMENT, strerror(errno));
  }
  return Status::Ok();
}

Status Socket::Bind(IpAddr addr) {
  if (!IsValid()) {
    return Status(StatusCode::INVALID_ARGUMENT, "Invalid socket");
  }
  if (bind(socket_, &addr.addr, sizeof(addr)) < 0) {
    auto err = errno;
    std::ostringstream msg;
    msg << "Bind to " << addr << " failed: " << strerror(err);
    return Status(StatusCode::INVALID_ARGUMENT, msg.str());
  }
  return Status::Ok();
}

Status Socket::SendTo(Slice data, int flags, IpAddr dest) {
  const auto result = sendto(socket_, data.begin(), data.length(), flags,
                             &dest.addr, sizeof(dest));
  if (result < 0) {
    return Status(StatusCode::UNKNOWN, strerror(errno))
        .WithContext("sendto failed");
  }
  if (size_t(result) != data.length()) {
    return Status(StatusCode::UNKNOWN, "Partial sendto");
  }
  return Status::Ok();
}

StatusOr<Socket::DataAndAddr> Socket::RecvFrom(size_t maximum_packet_size,
                                               int flags) {
  auto msg = Slice::WithInitializer(maximum_packet_size, [](uint8_t*) {});
  IpAddr source_address;
  socklen_t source_address_length = sizeof(source_address);

  auto result =
      recvfrom(socket_, const_cast<uint8_t*>(msg.begin()), msg.length(), 0,
               &source_address.addr, &source_address_length);
  if (result < 0) {
    return Status(StatusCode::UNKNOWN, strerror(errno))
        .WithContext("recvfrom failed");
  }
  msg.TrimEnd(msg.length() - result);
  assert(msg.length() == (size_t)result);
  return DataAndAddr{std::move(msg), std::move(source_address)};
}

}  // namespace overnet
