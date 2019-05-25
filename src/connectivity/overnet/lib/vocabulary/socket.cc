// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/vocabulary/socket.h"

#include <fcntl.h>
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

Status Socket::Create(int family, int type, int option) {
  *this = Socket(socket(family, type, option));
  if (socket_ == -1) {
    std::ostringstream msg;
    msg << "Creating socket family=" << family << " type=" << type
        << " option=" << option;
    return Status(StatusCode::UNKNOWN, strerror(errno))
        .WithContext(msg.str().c_str());
  }
  return Status::Ok();
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
    return Status(StatusCode::UNKNOWN, strerror(errno));
  }
  return Status::Ok();
}

Status Socket::MutateFlags(std::function<int(int)> mut) {
  if (!IsValid()) {
    return Status(StatusCode::INVALID_ARGUMENT, "Invalid socket");
  }
  int flags = fcntl(socket_, F_GETFL);
  if (flags == -1) {
    return Status(StatusCode::UNKNOWN, strerror(errno))
        .WithContext("fcntl(F_GETFL)");
  }
  flags = mut(flags);
  if (0 != fcntl(socket_, F_SETFL, flags)) {
    return Status(StatusCode::UNKNOWN, strerror(errno))
        .WithContext("fcntl(F_SETFL)");
  }
  return Status::Ok();
}

Status Socket::SetNonBlocking(bool non_blocking) {
  if (non_blocking) {
    return MutateFlags([](int flags) { return flags | O_NONBLOCK; });
  } else {
    return MutateFlags([](int flags) { return flags ^ ~O_NONBLOCK; });
  }
}

Status Socket::Bind(IpAddr addr) {
  if (!IsValid()) {
    return Status(StatusCode::INVALID_ARGUMENT, "Invalid socket");
  }
#ifndef __Fuchsia__
  // If we're using unix domain sockets, newest process always gets to handle
  // the socket.
  if (addr.addr.sa_family == AF_UNIX) {
    unlink(addr.unix.sun_path);
  }
#endif
  if (bind(socket_, addr.get(), addr.length()) < 0) {
    return Status(StatusCode::UNKNOWN, strerror(errno)).WithLazyContext([&] {
      std::ostringstream out;
      out << "bind(" << addr << ")";
      return out.str();
    });
  }
  return Status::Ok();
}

Status Socket::Connect(IpAddr addr) {
  if (!IsValid()) {
    return Status(StatusCode::INVALID_ARGUMENT, "Invalid socket");
  }
  if (0 != connect(socket_, addr.get(), addr.length())) {
    return Status(StatusCode::UNKNOWN, strerror(errno)).WithLazyContext([&] {
      std::ostringstream out;
      out << "connect(" << addr << ")";
      return out.str();
    });
  }
  return Status::Ok();
}

Status Socket::SendTo(Slice data, int flags, IpAddr dest) {
  const auto result = sendto(socket_, data.begin(), data.length(), flags,
                             dest.get(), dest.length());
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

Status Socket::Listen() {
  if (listen(socket_, 0) == 0) {
    return Status::Ok();
  } else if (errno == EINTR) {
    return Listen();
  } else {
    return Status(StatusCode::UNKNOWN, strerror(errno)).WithContext("listen");
  }
}

StatusOr<Socket> Socket::Accept() {
  int socket = accept(socket_, nullptr, nullptr);
  if (socket >= 0) {
    return Socket(socket);
  } else if (errno == EAGAIN) {
    return Status::Unavailable();
  } else if (errno == EINTR) {
    return Accept();
  } else {
    return Status(StatusCode::UNKNOWN, strerror(errno)).WithContext("accept");
  }
}

StatusOr<Slice> Socket::Write(Slice slice) {
  if (auto n = write(socket_, slice.begin(), slice.length()); n >= 0) {
    return slice.FromOffset(n);
  } else if (errno == EAGAIN) {
    return slice;
  } else if (errno == EINTR) {
    return Write(std::move(slice));
  } else {
    return Status(StatusCode::UNKNOWN, strerror(errno)).WithContext("write");
  }
}

StatusOr<Optional<Slice>> Socket::Read(size_t maximum_read_size) {
  auto msg = Slice::WithInitializer(maximum_read_size, [](uint8_t*) {});
  ssize_t n;
  for (;;) {
    n = read(socket_, const_cast<uint8_t*>(msg.begin()), msg.length());
    if (n == 0) {
      return Nothing;
    } else if (n > 0) {
      return msg.ToOffset(n);
    } else if (errno == EAGAIN) {
      return Slice();
    } else if (errno == EINTR) {
      continue;
    } else {
      return Status(StatusCode::UNKNOWN, strerror(errno)).WithContext("read");
    }
    abort();
  }
}

}  // namespace overnet
