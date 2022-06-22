// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "udp_serde.h"

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

#include <iostream>

#include <gtest/gtest.h>

namespace fnet = fuchsia_net;

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

class span : public cpp20::span<const uint8_t> {
 public:
  span(const uint8_t* data, size_t size) : cpp20::span<const uint8_t>(data, size) {}

  bool operator==(const span& other) const {
    return std::equal(begin(), end(), other.begin(), other.end());
  }

  friend std::ostream& operator<<(std::ostream& out, const span& buf);
};

std::ostream& operator<<(std::ostream& out, const span& span) {
  out << '[';
  const std::ios_base::fmtflags flags = out.flags();
  out.flags(std::ios::hex | std::ios::showbase);
  for (auto it = span.begin(); it != span.end(); ++it) {
    if (it != span.begin()) {
      out << ", ";
    }
    out << static_cast<unsigned int>(*it);
  }
  out.flags(flags);
  out << ']';
  return out;
}

class UdpSerdeTest : public ::testing::TestWithParam<AddrKind> {};

TEST_P(UdpSerdeTest, SendSerializeThenDeserialize) {
  fidl::Arena alloc;
  fidl::WireTableBuilder<fsocket::wire::SendMsgMeta> meta_builder =
      fsocket::wire::SendMsgMeta::Builder(alloc);
  fnet::wire::SocketAddress socket_addr;
  IpAddrType expected_addr_type;
  const uint8_t* expected_addr;
  switch (GetParam().Kind()) {
    case AddrKind::Kind::V4: {
      fnet::wire::Ipv4Address ipv4_addr;
      ipv4_addr.addr = kIPv4Addr;
      fnet::wire::Ipv4SocketAddress ipv4_socket_addr;
      ipv4_socket_addr.address = ipv4_addr;
      ipv4_socket_addr.port = kPort;
      socket_addr = fnet::wire::SocketAddress::WithIpv4(alloc, ipv4_socket_addr);
      expected_addr_type = IpAddrType::Ipv4;
      expected_addr = kIPv4Addr.data();
    } break;
    case AddrKind::Kind::V6: {
      fnet::wire::Ipv6Address ipv6_addr;
      ipv6_addr.addr = kIPv6Addr;
      fnet::wire::Ipv6SocketAddress ipv6_socket_addr;
      ipv6_socket_addr.address = ipv6_addr;
      ipv6_socket_addr.port = kPort;
      socket_addr = fnet::wire::SocketAddress::WithIpv6(alloc, ipv6_socket_addr);
      expected_addr_type = IpAddrType::Ipv6;
      expected_addr = kIPv6Addr.data();
    }
  }
  meta_builder.to(socket_addr);

  uint8_t kBuf[kTxUdpPreludeSize];
  fsocket::wire::SendMsgMeta meta = meta_builder.Build();
  ASSERT_TRUE(serialize_send_msg_meta(meta, cpp20::span<uint8_t>(kBuf, kTxUdpPreludeSize)));

  const Buffer in_buf = {
      .buf = kBuf,
      .buf_size = kTxUdpPreludeSize,
  };

  const DeserializeSendMsgMetaResult res = deserialize_send_msg_meta(in_buf);

  ASSERT_EQ(res.err, DeserializeSendMsgMetaErrorNone);
  EXPECT_EQ(res.port, kPort);
  EXPECT_EQ(res.to_addr.addr_type, expected_addr_type);
  const size_t addr_len = GetParam().AddrLen();
  const span found_addr(res.to_addr.addr, res.to_addr.addr_size);
  const span expected(expected_addr, addr_len);
  EXPECT_EQ(found_addr, expected);
}

TEST_P(UdpSerdeTest, RecvSerializeThenDeserialize) {
  const size_t addr_len = GetParam().AddrLen();
  uint8_t addr[kIPv6AddrLen];
  IpAddrType addr_type;
  CmsgSet cmsg_set = {
      .has_timestamp_nanos = true,
      .timestamp_nanos = 42,
  };
  switch (GetParam().Kind()) {
    case AddrKind::Kind::V4: {
      addr_type = IpAddrType::Ipv4;
      memcpy(addr, kIPv4Addr.data(), addr_len);
      cmsg_set.has_ip_tos = true;
      cmsg_set.ip_tos = 43;
      cmsg_set.has_ip_ttl = true;
      cmsg_set.ip_ttl = 44;
    } break;
    case AddrKind::Kind::V6: {
      addr_type = IpAddrType::Ipv6;
      memcpy(addr, kIPv6Addr.data(), addr_len);
      cmsg_set.has_ipv6_tclass = true;
      cmsg_set.ipv6_tclass = 45;
      cmsg_set.has_ipv6_hoplimit = true;
      cmsg_set.ipv6_hoplimit = 46;
      cmsg_set.has_ipv6_pktinfo = true;
      cmsg_set.ipv6_pktinfo = {
          .if_index = 47,
          .addr =
              {
                  .buf = addr,
                  .buf_size = addr_len,
              },
      };
    }
  }

  const ConstBuffer addr_buf = {
      .buf = addr,
      .buf_size = addr_len,
  };

  uint8_t kBuf[kRxUdpPreludeSize];

  const Buffer out_buf = {
      .buf = kBuf,
      .buf_size = kRxUdpPreludeSize,
  };
  constexpr size_t kPayloadSize = 41;
  ASSERT_EQ(serialize_recv_msg_meta(
                {
                    .cmsg_set = cmsg_set,
                    .from_addr_type = addr_type,
                    .payload_size = kPayloadSize,
                    .port = kPort,
                },
                addr_buf, out_buf),
            SerializeRecvMsgMetaErrorNone);
  fidl::unstable::DecodedMessage<fsocket::wire::RecvMsgMeta> decoded =
      deserialize_recv_msg_meta(cpp20::span<uint8_t>(out_buf.buf, out_buf.buf_size));
  ASSERT_TRUE(decoded.ok());

  const fsocket::wire::RecvMsgMeta& recv_meta = *decoded.PrimaryObject();

  ASSERT_TRUE(recv_meta.has_control());
  const fsocket::wire::DatagramSocketRecvControlData& control = recv_meta.control();

  ASSERT_TRUE(control.has_network());
  const fsocket::wire::NetworkSocketRecvControlData& network_control = control.network();

  ASSERT_TRUE(network_control.has_socket());
  const fsocket::wire::SocketRecvControlData& socket_control = network_control.socket();

  ASSERT_TRUE(socket_control.has_timestamp());
  EXPECT_EQ(socket_control.timestamp().nanoseconds(), cmsg_set.timestamp_nanos);

  ASSERT_TRUE(recv_meta.has_from());
  const fnet::wire::SocketAddress& from = recv_meta.from();

  switch (GetParam().Kind()) {
    case AddrKind::Kind::V4: {
      ASSERT_EQ(from.Which(), fnet::wire::SocketAddress::Tag::kIpv4);
      EXPECT_EQ(from.ipv4().port, kPort);
      const span found_addr(from.ipv4().address.addr.data(), from.ipv4().address.addr.size());
      const span expected_addr(kIPv4Addr.data(), kIPv4Addr.size());
      EXPECT_EQ(found_addr, expected_addr);

      ASSERT_TRUE(network_control.has_ip());
      const fsocket::wire::IpRecvControlData& ip_control = network_control.ip();

      ASSERT_TRUE(ip_control.has_tos());
      EXPECT_EQ(ip_control.tos(), cmsg_set.ip_tos);

      ASSERT_TRUE(ip_control.has_ttl());
      EXPECT_EQ(ip_control.ttl(), cmsg_set.ip_ttl);

      EXPECT_FALSE(network_control.has_ipv6());
    } break;
    case AddrKind::Kind::V6: {
      ASSERT_EQ(from.Which(), fnet::wire::SocketAddress::Tag::kIpv6);
      EXPECT_EQ(from.ipv6().port, kPort);
      const span found_addr(from.ipv6().address.addr.data(), from.ipv6().address.addr.size());
      const span expected_addr(kIPv6Addr.data(), kIPv6Addr.size());
      EXPECT_EQ(found_addr, expected_addr);

      ASSERT_TRUE(recv_meta.control().network().has_ipv6());
      const fsocket::wire::Ipv6RecvControlData& ipv6_control = network_control.ipv6();

      ASSERT_TRUE(ipv6_control.has_tclass());
      EXPECT_EQ(ipv6_control.tclass(), cmsg_set.ipv6_tclass);

      ASSERT_TRUE(ipv6_control.has_hoplimit());
      EXPECT_EQ(ipv6_control.hoplimit(), cmsg_set.ipv6_hoplimit);

      ASSERT_TRUE(ipv6_control.has_pktinfo());
      EXPECT_EQ(ipv6_control.pktinfo().iface, cmsg_set.ipv6_pktinfo.if_index);
      span found_pktinfo_addr(ipv6_control.pktinfo().header_destination_addr.addr.data(),
                              ipv6_control.pktinfo().header_destination_addr.addr.size());
      span expected_pktinfo_addr(cmsg_set.ipv6_pktinfo.addr.buf,
                                 cmsg_set.ipv6_pktinfo.addr.buf_size);
      EXPECT_EQ(found_pktinfo_addr, expected_pktinfo_addr);
      EXPECT_FALSE(network_control.has_ip());
    }
  }

  EXPECT_EQ(recv_meta.payload_len(), kPayloadSize);
}

TEST_P(UdpSerdeTest, DeserializeRecvErrors) {
  uint8_t kBuf[kRxUdpPreludeSize];
  memset(kBuf, 0, kRxUdpPreludeSize);

  // Buffer too short.
  fidl::unstable::DecodedMessage<fsocket::wire::RecvMsgMeta> buffer_too_short =
      deserialize_recv_msg_meta(cpp20::span<uint8_t>(kBuf, 0));
  EXPECT_FALSE(buffer_too_short.ok());

  // Nonzero prelude.
  memset(kBuf, 1, kRxUdpPreludeSize);
  fidl::unstable::DecodedMessage<fsocket::wire::RecvMsgMeta> nonzero_prelude =
      deserialize_recv_msg_meta(cpp20::span<uint8_t>(kBuf, kRxUdpPreludeSize));
  EXPECT_FALSE(nonzero_prelude.ok());

  // Meta size too large.
  memset(kBuf, 0, kRxUdpPreludeSize);
  uint16_t meta_size = std::numeric_limits<uint16_t>::max();
  memcpy(kBuf, &meta_size, sizeof(meta_size));
  fidl::unstable::DecodedMessage<fsocket::wire::RecvMsgMeta> meta_exceeds_buf =
      deserialize_recv_msg_meta(cpp20::span<uint8_t>(kBuf, static_cast<uint64_t>(meta_size - 1)));
  EXPECT_FALSE(meta_exceeds_buf.ok());

  // Failed to decode.
  memset(kBuf, 0, kRxUdpPreludeSize);
  fidl::unstable::DecodedMessage<fsocket::wire::RecvMsgMeta> failed_to_decode =
      deserialize_recv_msg_meta(cpp20::span<uint8_t>(kBuf, kRxUdpPreludeSize));
  EXPECT_FALSE(failed_to_decode.ok());
}

TEST_P(UdpSerdeTest, DeserializeSendErrors) {
  // Null buffer.
  const DeserializeSendMsgMetaResult null_buffer = deserialize_send_msg_meta({
      .buf = nullptr,
      .buf_size = 0,
  });
  EXPECT_EQ(null_buffer.err, DeserializeSendMsgMetaErrorInputBufferNull);

  uint8_t kBuf[kTxUdpPreludeSize];

  // Buffer too short.
  const DeserializeSendMsgMetaResult buf_too_short = deserialize_send_msg_meta({
      .buf = kBuf,
      .buf_size = 0,
  });
  EXPECT_EQ(buf_too_short.err, DeserializeSendMsgMetaErrorInputBufferTooSmall);

  // Nonzero prelude.
  memset(kBuf, 1, kTxUdpPreludeSize);
  const DeserializeSendMsgMetaResult nonzero_prelude = deserialize_send_msg_meta({
      .buf = kBuf,
      .buf_size = kTxUdpPreludeSize,
  });
  EXPECT_EQ(nonzero_prelude.err, DeserializeSendMsgMetaErrorNonZeroPrelude);

  // Meta size too large.
  memset(kBuf, 0, kTxUdpPreludeSize);
  uint16_t meta_size = std::numeric_limits<uint16_t>::max();
  memcpy(kBuf, &meta_size, sizeof(meta_size));
  const DeserializeSendMsgMetaResult meta_exceeds_buf = deserialize_send_msg_meta({
      .buf = kBuf,
      .buf_size = static_cast<uint64_t>(meta_size - 1),
  });
  EXPECT_EQ(meta_exceeds_buf.err, DeserializeSendMsgMetaErrorInputBufferTooSmall);

  // Failed to decode.
  memset(kBuf, 0, kTxUdpPreludeSize);
  const DeserializeSendMsgMetaResult failed_to_decode = deserialize_send_msg_meta({
      .buf = kBuf,
      .buf_size = kTxUdpPreludeSize,
  });
  EXPECT_EQ(failed_to_decode.err, DeserializeSendMsgMetaErrorFailedToDecode);
}

TEST_P(UdpSerdeTest, SerializeSendErrors) {
  fsocket::wire::SendMsgMeta meta;
  uint8_t kBuf[kTxUdpPreludeSize];
  EXPECT_FALSE(serialize_send_msg_meta(meta, cpp20::span<uint8_t>(kBuf, 0)));
}

TEST_P(UdpSerdeTest, SerializeRecvErrors) {
  const size_t addr_len = GetParam().AddrLen();
  uint8_t addr[kIPv6AddrLen];
  RecvMsgMeta meta = RecvMsgMeta();
  switch (GetParam().Kind()) {
    case AddrKind::Kind::V4: {
      std::memmove(addr, kIPv4Addr.data(), addr_len);
      meta.from_addr_type = IpAddrType::Ipv4;
    } break;
    case AddrKind::Kind::V6: {
      std::memmove(addr, kIPv6Addr.data(), addr_len);
      meta.from_addr_type = IpAddrType::Ipv6;
    }
  }

  const ConstBuffer addr_buf = {
      .buf = addr,
      .buf_size = addr_len,
  };

  // Output buffer null.
  EXPECT_EQ(serialize_recv_msg_meta(meta, addr_buf,
                                    {
                                        .buf = nullptr,
                                        .buf_size = 0,
                                    }),
            SerializeRecvMsgMetaErrorOutputBufferNull);

  uint8_t kBuf[kTxUdpPreludeSize];

  // Output buffer too short.
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

  // Ipv6PktInfo buffer null.
  meta.cmsg_set = {
      .has_ipv6_pktinfo = true,
      .ipv6_pktinfo =
          {
              .addr =
                  {
                      .buf = nullptr,
                      .buf_size = 0,
                  },
          },
  };
  EXPECT_EQ(serialize_recv_msg_meta(meta, addr_buf,
                                    {
                                        .buf = kBuf,
                                        .buf_size = kTxUdpPreludeSize,
                                    }),
            SerializeRecvMsgMetaErrorIpv6PktInfoAddrNull);

  // Ipv6PktInfo buffer wrong size.
  meta.cmsg_set.ipv6_pktinfo.addr = {
      .buf = addr,
      .buf_size = 0,
  };
  EXPECT_EQ(serialize_recv_msg_meta(meta, addr_buf,
                                    {
                                        .buf = kBuf,
                                        .buf_size = kTxUdpPreludeSize,
                                    }),
            SerializeRecvMsgMetaErrorIpv6PktInfoAddrWrongSize);
}

INSTANTIATE_TEST_SUITE_P(UdpSerdeTest, UdpSerdeTest,
                         ::testing::Values(AddrKind::Kind::V4, AddrKind::Kind::V6),
                         [](const auto info) { return info.param.AddrKindToString(); });
