// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "udp_serde.h"

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

#include <iostream>

#include <gtest/gtest.h>

#include "udp_serde_test_util.h"

namespace fnet = fuchsia_net;

class UdpSerdeTest : public ::testing::TestWithParam<AddrKind> {};

TEST_P(UdpSerdeTest, SendSerializeThenDeserialize) {
  TestSendMsgMeta meta(GetParam().GetKind());
  uint8_t kBuf[kTxUdpPreludeSize];
  fidl::Arena alloc;
  fsocket::wire::SendMsgMeta fidl = meta.Get(alloc);
  ASSERT_EQ(serialize_send_msg_meta(fidl, cpp20::span<uint8_t>(kBuf, kTxUdpPreludeSize)),
            SerializeSendMsgMetaErrorNone);

  const Buffer in_buf = {
      .buf = kBuf,
      .buf_size = kTxUdpPreludeSize,
  };

  const DeserializeSendMsgMetaResult res = deserialize_send_msg_meta(in_buf);

  ASSERT_EQ(res.err, DeserializeSendMsgMetaErrorNone);
  EXPECT_EQ(res.port, meta.Port());
  EXPECT_EQ(res.to_addr.addr_type, meta.AddrType());
  const span found_addr(res.to_addr.addr, meta.AddrLen());
  const span expected(meta.Addr(), meta.AddrLen());
  EXPECT_EQ(found_addr, expected);
}

TEST_P(UdpSerdeTest, SendSerializeFromAddrThenDeserialize) {
  TestSendMsgMeta meta(GetParam().GetKind());
  IpAddress addr{
      .addr_type = meta.AddrType(),
  };
  memcpy(addr.addr, meta.Addr(), meta.AddrLen());
  uint8_t kBuf[kTxUdpPreludeSize];
  Buffer buf = {
      .buf = kBuf,
      .buf_size = kTxUdpPreludeSize,
  };
  ASSERT_EQ(serialize_send_msg_meta_from_addr(addr, meta.Port(), buf),
            SerializeSendMsgMetaErrorNone);

  const DeserializeSendMsgMetaResult res = deserialize_send_msg_meta(buf);

  ASSERT_EQ(res.err, DeserializeSendMsgMetaErrorNone);
  EXPECT_EQ(res.port, meta.Port());
  EXPECT_EQ(res.to_addr.addr_type, meta.AddrType());
  const span found_addr(res.to_addr.addr, meta.AddrLen());
  const span expected(meta.Addr(), meta.AddrLen());
  EXPECT_EQ(found_addr, expected);
}

TEST_P(UdpSerdeTest, RecvSerializeThenDeserialize) {
  const auto& [test_recv_meta, addr_buf] = GetTestRecvMsgMeta(GetParam().GetKind());
  const CmsgSet& cmsg_set = test_recv_meta.cmsg_set;

  uint8_t kBuf[kRxUdpPreludeSize];

  const Buffer out_buf = {
      .buf = kBuf,
      .buf_size = kRxUdpPreludeSize,
  };
  ASSERT_EQ(serialize_recv_msg_meta(&test_recv_meta, addr_buf, out_buf),
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
  EXPECT_EQ(socket_control.timestamp().nanoseconds, cmsg_set.timestamp_nanos);

  ASSERT_TRUE(recv_meta.has_from());
  const fnet::wire::SocketAddress& from = recv_meta.from();

  switch (GetParam().GetKind()) {
    case AddrKind::Kind::V4: {
      ASSERT_EQ(from.Which(), fnet::wire::SocketAddress::Tag::kIpv4);
      EXPECT_EQ(from.ipv4().port, test_recv_meta.port);
      const span found_addr(from.ipv4().address.addr.data(), from.ipv4().address.addr.size());
      const span expected_addr(addr_buf.buf, addr_buf.buf_size);
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
      EXPECT_EQ(from.ipv6().port, test_recv_meta.port);
      const span found_addr(from.ipv6().address.addr.data(), from.ipv6().address.addr.size());
      const span expected_addr(addr_buf.buf, addr_buf.buf_size);
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
      span expected_pktinfo_addr(cmsg_set.ipv6_pktinfo.addr);
      EXPECT_EQ(found_pktinfo_addr, expected_pktinfo_addr);
      EXPECT_FALSE(network_control.has_ip());
    }
  }

  EXPECT_EQ(recv_meta.payload_len(), test_recv_meta.payload_size);
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
  EXPECT_EQ(serialize_send_msg_meta(meta, cpp20::span<uint8_t>(kBuf, 0)),
            SerializeSendMsgMetaErrorOutputBufferTooSmall);
}

TEST_P(UdpSerdeTest, SerializeSendFromAddrErrors) {
  TestSendMsgMeta meta(GetParam().GetKind());
  IpAddress addr{
      .addr_type = meta.AddrType(),
  };
  memcpy(addr.addr, meta.Addr(), meta.AddrLen());
  uint8_t kBuf[kTxUdpPreludeSize];

  // Output buffer null.
  EXPECT_EQ(serialize_send_msg_meta_from_addr(addr, meta.Port(),
                                              {
                                                  .buf = nullptr,
                                                  .buf_size = 0,
                                              }),
            SerializeSendMsgMetaErrorOutputBufferNull);

  // Output buffer too short.
  EXPECT_EQ(serialize_send_msg_meta_from_addr(addr, meta.Port(),
                                              {
                                                  .buf = kBuf,
                                                  .buf_size = 0,
                                              }),
            SerializeSendMsgMetaErrorOutputBufferTooSmall);
}

TEST_P(UdpSerdeTest, SerializeRecvErrors) {
  const size_t addr_len = GetParam().Len();
  uint8_t addr[GetParam().Len()];
  RecvMsgMeta meta = {
      .from_addr_type = GetParam().ToAddrType(),
  };

  const ConstBuffer addr_buf = {
      .buf = addr,
      .buf_size = addr_len,
  };

  // Output buffer null.
  EXPECT_EQ(serialize_recv_msg_meta(&meta, addr_buf,
                                    {
                                        .buf = nullptr,
                                        .buf_size = 0,
                                    }),
            SerializeRecvMsgMetaErrorOutputBufferNull);

  uint8_t kBuf[kTxUdpPreludeSize];

  // Output buffer too short.
  EXPECT_EQ(serialize_recv_msg_meta(&meta, addr_buf,
                                    {
                                        .buf = kBuf,
                                        .buf_size = 0,
                                    }),
            SerializeRecvMsgMetaErrorOutputBufferTooSmall);

  // Address too short.
  EXPECT_EQ(serialize_recv_msg_meta(&meta,
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
                         [](const auto info) { return info.param.ToString(); });
