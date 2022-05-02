// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "udp_serde.h"

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

#include <gtest/gtest.h>

namespace {
constexpr uint16_t kPort = 80;
constexpr size_t kIPv4AddrLen = 4;
constexpr size_t kIPv6AddrLen = 16;
constexpr fidl::Array<uint8_t, kIPv4AddrLen> kIPv4Addr = {0x1, 0x2, 0x3, 0x4};
constexpr fidl::Array<uint8_t, kIPv6AddrLen> kIPv6Addr = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                                                          0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
}  // namespace

class AddrKind {
 public:
  enum class Kind {
    V4,
    V6,
  };

  explicit AddrKind(enum Kind kind) : kind(kind) {}
  enum Kind Kind() const { return kind; }

  constexpr const char* AddrKindToString() const {
    switch (kind) {
      case Kind::V4:
        return "V4";
      case Kind::V6:
        return "V6";
    }
  }

  size_t AddrLen() const {
    switch (kind) {
      case Kind::V4:
        return kIPv4AddrLen;
      case Kind::V6:
        return kIPv6AddrLen;
    }
  }

 private:
  enum Kind kind;
};

class UdpSerdeTest : public ::testing::TestWithParam<AddrKind> {};

TEST_P(UdpSerdeTest, SendSerializeThenDeserialize) {
  fidl::Arena alloc;
  fidl::WireTableBuilder<fuchsia_posix_socket::wire::SendMsgMeta> meta_builder =
      fuchsia_posix_socket::wire::SendMsgMeta::Builder(alloc);
  fuchsia_net::wire::SocketAddress socket_addr;
  IpAddrType expected_addr_type;
  const uint8_t* expected_addr;
  switch (GetParam().Kind()) {
    case AddrKind::Kind::V4: {
      fuchsia_net::wire::Ipv4Address ipv4_addr;
      ipv4_addr.addr = kIPv4Addr;
      fuchsia_net::wire::Ipv4SocketAddress ipv4_socket_addr;
      ipv4_socket_addr.address = ipv4_addr;
      ipv4_socket_addr.port = kPort;
      socket_addr = fuchsia_net::wire::SocketAddress::WithIpv4(alloc, ipv4_socket_addr);
      expected_addr_type = IpAddrType::Ipv4;
      expected_addr = kIPv4Addr.begin();
    } break;
    case AddrKind::Kind::V6: {
      fuchsia_net::wire::Ipv6Address ipv6_addr;
      ipv6_addr.addr = kIPv6Addr;
      fuchsia_net::wire::Ipv6SocketAddress ipv6_socket_addr;
      ipv6_socket_addr.address = ipv6_addr;
      ipv6_socket_addr.port = kPort;
      socket_addr = fuchsia_net::wire::SocketAddress::WithIpv6(alloc, ipv6_socket_addr);
      expected_addr_type = IpAddrType::Ipv6;
      expected_addr = kIPv6Addr.begin();
    }
  }
  meta_builder.to(socket_addr);

  uint8_t kBuf[kTxUdpPreludeSize];
  fuchsia_posix_socket::wire::SendMsgMeta meta = meta_builder.Build();
  ASSERT_TRUE(serialize_send_msg_meta(meta, cpp20::span<uint8_t>(kBuf, kTxUdpPreludeSize)));

  Buffer in_buf = Buffer{
      .buf = kBuf,
      .buf_size = kTxUdpPreludeSize,
  };

  DeserializeSendMsgMetaResult res = deserialize_send_msg_meta(in_buf);

  ASSERT_EQ(res.err, DeserializeSendMsgMetaErrorNone);
  EXPECT_EQ(res.port, kPort);
  EXPECT_EQ(res.to_addr.addr_type, expected_addr_type);
  EXPECT_EQ(res.to_addr.addr_size, GetParam().AddrLen());
  EXPECT_EQ(0, memcmp(res.to_addr.addr, expected_addr, GetParam().AddrLen()));
}

TEST_P(UdpSerdeTest, RecvSerializeThenDeserialize) {
  const size_t addr_len = GetParam().AddrLen();
  uint8_t addr[kIPv6AddrLen];
  IpAddrType addr_type;
  switch (GetParam().Kind()) {
    case AddrKind::Kind::V4: {
      addr_type = IpAddrType::Ipv4;
      memcpy(addr, kIPv4Addr.begin(), addr_len);
    } break;
    case AddrKind::Kind::V6: {
      addr_type = IpAddrType::Ipv6;
      memcpy(addr, kIPv6Addr.begin(), addr_len);
    }
  }

  const Buffer addr_buf = Buffer{
      .buf = addr,
      .buf_size = addr_len,
  };

  const CmsgSet cmsg_set = CmsgSet{
      .has_ip_tos = true,
      .ip_tos = 42,
      .has_ip_ttl = true,
      .ip_ttl = 43,
      .has_ipv6_tclass = true,
      .ipv6_tclass = 44,
      .has_ipv6_hoplimit = true,
      .ipv6_hoplimit = 45,
      .has_timestamp_nanos = true,
      .timestamp_nanos = 46,
  };

  uint8_t kBuf[kRxUdpPreludeSize];

  const Buffer out_buf = Buffer{
      .buf = kBuf,
      .buf_size = kRxUdpPreludeSize,
  };
  constexpr size_t kPayloadSize = 41;
  ASSERT_EQ(serialize_recv_msg_meta(
                RecvMsgMeta{
                    .cmsg_set = cmsg_set,
                    .from_addr_type = addr_type,
                    .payload_size = kPayloadSize,
                    .port = kPort,
                },
                addr_buf, out_buf),
            SerializeRecvMsgMetaErrorNone);
  fidl::unstable::DecodedMessage<fuchsia_posix_socket::wire::RecvMsgMeta> decoded =
      deserialize_recv_msg_meta(cpp20::span<uint8_t>(out_buf.buf, out_buf.buf_size));
  ASSERT_TRUE(decoded.ok());

  fuchsia_posix_socket::wire::RecvMsgMeta* recv_meta = decoded.PrimaryObject();

  switch (GetParam().Kind()) {
    case AddrKind::Kind::V4: {
      EXPECT_EQ(recv_meta->from().Which(), fuchsia_net::wire::SocketAddress::Tag::kIpv4);
      EXPECT_EQ(recv_meta->from().ipv4().port, kPort);
      EXPECT_EQ(0, memcmp(recv_meta->from().ipv4().address.addr.begin(), kIPv4Addr.begin(),
                          kIPv4AddrLen));
    } break;
    case AddrKind::Kind::V6: {
      EXPECT_EQ(recv_meta->from().Which(), fuchsia_net::wire::SocketAddress::Tag::kIpv6);
      EXPECT_EQ(recv_meta->from().ipv6().port, kPort);
      EXPECT_EQ(0, memcmp(recv_meta->from().ipv6().address.addr.begin(), kIPv6Addr.begin(),
                          kIPv6AddrLen));
    }
  }

  EXPECT_EQ(recv_meta->control().network().ip().has_tos(), cmsg_set.has_ip_tos);
  EXPECT_EQ(recv_meta->control().network().ip().tos(), cmsg_set.ip_tos);

  EXPECT_EQ(recv_meta->control().network().ip().has_ttl(), cmsg_set.has_ip_ttl);
  EXPECT_EQ(recv_meta->control().network().ip().ttl(), cmsg_set.ip_ttl);

  EXPECT_EQ(recv_meta->control().network().ipv6().has_tclass(), cmsg_set.has_ipv6_tclass);
  EXPECT_EQ(recv_meta->control().network().ipv6().tclass(), cmsg_set.ipv6_tclass);

  EXPECT_EQ(recv_meta->control().network().ipv6().has_hoplimit(), cmsg_set.has_ipv6_hoplimit);
  EXPECT_EQ(recv_meta->control().network().ipv6().hoplimit(), cmsg_set.ipv6_hoplimit);

  EXPECT_EQ(recv_meta->control().network().socket().has_timestamp(), cmsg_set.has_timestamp_nanos);
  EXPECT_EQ(recv_meta->control().network().socket().timestamp().nanoseconds(),
            cmsg_set.timestamp_nanos);

  EXPECT_EQ(recv_meta->payload_len(), kPayloadSize);
}

TEST_P(UdpSerdeTest, DeserializeRecvErrors) {
  uint8_t kBuf[kRxUdpPreludeSize];
  memset(kBuf, 0, kRxUdpPreludeSize);

  // Buffer too short.
  fidl::unstable::DecodedMessage<fuchsia_posix_socket::wire::RecvMsgMeta> buffer_too_short =
      deserialize_recv_msg_meta(cpp20::span<uint8_t>(kBuf, 0));
  EXPECT_FALSE(buffer_too_short.ok());

  // Nonzero prelude.
  memset(kBuf, 1, kRxUdpPreludeSize);
  fidl::unstable::DecodedMessage<fuchsia_posix_socket::wire::RecvMsgMeta> nonzero_prelude =
      deserialize_recv_msg_meta(cpp20::span<uint8_t>(kBuf, kRxUdpPreludeSize));
  EXPECT_FALSE(nonzero_prelude.ok());

  // Meta size too large.
  memset(kBuf, 0, kRxUdpPreludeSize);
  uint16_t meta_size = std::numeric_limits<uint16_t>::max();
  memcpy(kBuf, &meta_size, sizeof(meta_size));
  fidl::unstable::DecodedMessage<fuchsia_posix_socket::wire::RecvMsgMeta> meta_exceeds_buf =
      deserialize_recv_msg_meta(cpp20::span<uint8_t>(kBuf, static_cast<uint64_t>(meta_size - 1)));
  EXPECT_FALSE(meta_exceeds_buf.ok());

  // Failed to decode.
  memset(kBuf, 0, kRxUdpPreludeSize);
  fidl::unstable::DecodedMessage<fuchsia_posix_socket::wire::RecvMsgMeta> failed_to_decode =
      deserialize_recv_msg_meta(cpp20::span<uint8_t>(kBuf, kRxUdpPreludeSize));
  EXPECT_FALSE(failed_to_decode.ok());
}

TEST_P(UdpSerdeTest, DeserializeSendErrors) {
  // Null buffer.
  DeserializeSendMsgMetaResult null_buffer = deserialize_send_msg_meta({
      .buf = nullptr,
      .buf_size = 0,
  });
  EXPECT_EQ(null_buffer.err, DeserializeSendMsgMetaErrorInputBufferNull);

  uint8_t kBuf[kTxUdpPreludeSize];

  // Buffer too short.
  DeserializeSendMsgMetaResult buf_too_short = deserialize_send_msg_meta({
      .buf = kBuf,
      .buf_size = 0,
  });
  EXPECT_EQ(buf_too_short.err, DeserializeSendMsgMetaErrorInputBufferTooSmall);

  // Nonzero prelude.
  memset(kBuf, 1, kTxUdpPreludeSize);
  DeserializeSendMsgMetaResult nonzero_prelude = deserialize_send_msg_meta({
      .buf = kBuf,
      .buf_size = kTxUdpPreludeSize,
  });
  EXPECT_EQ(nonzero_prelude.err, DeserializeSendMsgMetaErrorNonZeroPrelude);

  // Meta size too large.
  memset(kBuf, 0, kTxUdpPreludeSize);
  uint16_t meta_size = std::numeric_limits<uint16_t>::max();
  memcpy(kBuf, &meta_size, sizeof(meta_size));
  DeserializeSendMsgMetaResult meta_exceeds_buf = deserialize_send_msg_meta({
      .buf = kBuf,
      .buf_size = static_cast<uint64_t>(meta_size - 1),
  });
  EXPECT_EQ(meta_exceeds_buf.err, DeserializeSendMsgMetaErrorInputBufferTooSmall);

  // Failed to decode.
  memset(kBuf, 0, kTxUdpPreludeSize);
  DeserializeSendMsgMetaResult failed_to_decode = deserialize_send_msg_meta({
      .buf = kBuf,
      .buf_size = kTxUdpPreludeSize,
  });
  EXPECT_EQ(failed_to_decode.err, DeserializeSendMsgMetaErrorFailedToDecode);
}

TEST_P(UdpSerdeTest, SerializeSendErrors) {
  fuchsia_posix_socket::wire::SendMsgMeta meta;
  uint8_t kBuf[kTxUdpPreludeSize];
  EXPECT_FALSE(serialize_send_msg_meta(meta, cpp20::span<uint8_t>(kBuf, 0)));
}

TEST_P(UdpSerdeTest, SerializeRecvErrors) {
  const size_t addr_len = GetParam().AddrLen();
  uint8_t addr[kIPv6AddrLen];
  RecvMsgMeta meta = RecvMsgMeta();
  switch (GetParam().Kind()) {
    case AddrKind::Kind::V4: {
      std::memmove(addr, kIPv4Addr.begin(), addr_len);
      meta.from_addr_type = IpAddrType::Ipv4;
    } break;
    case AddrKind::Kind::V6: {
      std::memmove(addr, kIPv6Addr.begin(), addr_len);
      meta.from_addr_type = IpAddrType::Ipv6;
    }
  }

  Buffer addr_buf = Buffer{
      .buf = addr,
      .buf_size = addr_len,
  };

  // Null buffer.
  EXPECT_EQ(serialize_recv_msg_meta(meta, addr_buf,
                                    {
                                        .buf = nullptr,
                                        .buf_size = 0,
                                    }),
            SerializeRecvMsgMetaErrorOutputBufferNull);

  uint8_t kBuf[kTxUdpPreludeSize];

  // Buffer too short.
  EXPECT_EQ(serialize_recv_msg_meta(meta, addr_buf,
                                    {
                                        .buf = kBuf,
                                        .buf_size = 0,
                                    }),
            SerializeRecvMsgMetaErrorOutputBufferTooSmall);

  // Address too short.
  EXPECT_EQ(serialize_recv_msg_meta(meta,
                                    {
                                        .buf = addr,
                                        .buf_size = 0,
                                    },
                                    {
                                        .buf = kBuf,
                                        .buf_size = kTxUdpPreludeSize,
                                    }),
            SerializeRecvMsgMetaErrorFromAddrBufferTooSmall);
}

INSTANTIATE_TEST_SUITE_P(UdpSerdeTest, UdpSerdeTest,
                         ::testing::Values(AddrKind::Kind::V4, AddrKind::Kind::V6),
                         [](const auto info) { return info.param.AddrKindToString(); });
