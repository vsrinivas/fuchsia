// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_CMSG_H_
#define LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_CMSG_H_

#include <fidl/fuchsia.posix.socket.packet/cpp/wire.h>
#include <fidl/fuchsia.posix.socket/cpp/wire.h>
#include <lib/fit/result.h>
#include <lib/zxio/cpp/dgram_cache.h>
#include <sys/socket.h>

#include <span>

class FidlControlDataProcessor {
 public:
  FidlControlDataProcessor(void* buf, socklen_t len)
      : buffer_(cpp20::span{reinterpret_cast<unsigned char*>(buf), len}) {}

  socklen_t Store(fuchsia_posix_socket::wire::DatagramSocketRecvControlData const& control_data,
                  const RequestedCmsgSet& requested);

  socklen_t Store(fuchsia_posix_socket::wire::NetworkSocketRecvControlData const& control_data,
                  const RequestedCmsgSet& requested);

  socklen_t Store(fuchsia_posix_socket_packet::wire::RecvControlData const& control_data,
                  const RequestedCmsgSet& requested);

 private:
  socklen_t Store(fuchsia_posix_socket::wire::SocketRecvControlData const& control_data,
                  const RequestedCmsgSet& requested);

  socklen_t Store(fuchsia_posix_socket::wire::IpRecvControlData const& control_data,
                  const RequestedCmsgSet& requested);

  socklen_t Store(fuchsia_posix_socket::wire::Ipv6RecvControlData const& control_data,
                  const RequestedCmsgSet& requested);

  socklen_t StoreControlMessage(int level, int type, const void* data, socklen_t len);

  cpp20::span<unsigned char> buffer_;
};

template <typename T>
fit::result<int16_t, T> ParseControlMessages(fidl::AnyArena& allocator, const struct msghdr& msg);

template <>
fit::result<int16_t, fuchsia_posix_socket::wire::DatagramSocketSendControlData>
ParseControlMessages<fuchsia_posix_socket::wire::DatagramSocketSendControlData>(
    fidl::AnyArena& allocator, const struct msghdr& msg);

template <>
fit::result<int16_t, fuchsia_posix_socket::wire::NetworkSocketSendControlData>
ParseControlMessages<fuchsia_posix_socket::wire::NetworkSocketSendControlData>(
    fidl::AnyArena& allocator, const struct msghdr& msg);

template <>
fit::result<int16_t, fuchsia_posix_socket::wire::SocketSendControlData>
ParseControlMessages<fuchsia_posix_socket::wire::SocketSendControlData>(fidl::AnyArena& allocator,
                                                                        const struct msghdr& msg);
template <>
fit::result<int16_t, fuchsia_posix_socket_packet::wire::SendControlData>
ParseControlMessages<fuchsia_posix_socket_packet::wire::SendControlData>(fidl::AnyArena& allocator,
                                                                         const struct msghdr& msg);

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_CMSG_H_
