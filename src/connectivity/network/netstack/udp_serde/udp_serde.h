// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_NETSTACK_UDP_SERDE_UDP_SERDE_H_
#define SRC_CONNECTIVITY_NETWORK_NETSTACK_UDP_SERDE_UDP_SERDE_H_

#include <ifaddrs.h>
#include <stdbool.h>

#define UDP_SERDE_EXPORT __attribute__((visibility("default")))

// `udp_serde` exposes methods for serializing and deserializing FIDL messages
// used in the Fast UDP protocol. These methods serialize using a custom wire
// format, including specialized mechanisms for padding and versioning.

// This library is highly customized for the needs of its two users (Netstack
// and fdio) and should not be relied upon by anyone else.

// TODO(https://fxbug.dev/97607): Consider replacing this library with FIDL-at-rest.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Buffer {
  uint8_t* buf;
  size_t buf_size;
} Buffer;

typedef struct ConstBuffer {
  const uint8_t* buf;
  size_t buf_size;
} ConstBuffer;

typedef enum IpAddrType { Ipv4, Ipv6 } IpAddrType;

typedef enum DeserializeSendMsgMetaError {
  DeserializeSendMsgMetaErrorNone,
  DeserializeSendMsgMetaErrorInputBufferNull,
  DeserializeSendMsgMetaErrorInputBufferTooSmall,
  DeserializeSendMsgMetaErrorFailedToDecode,
} DeserializeSendMsgMetaError;

#define kMaxIpAddrSize 16

typedef struct IpAddress {
  IpAddrType addr_type;
  uint8_t addr[kMaxIpAddrSize];
} IpAddress;

typedef struct Ipv6PktInfo {
  uint64_t if_index;
  uint8_t addr[kMaxIpAddrSize];
} Ipv6PktInfo;

typedef struct SendAndRecvCmsgSet {
  bool has_ip_ttl;
  uint8_t ip_ttl;

  bool has_ipv6_hoplimit;
  uint8_t ipv6_hoplimit;

  bool has_ipv6_pktinfo;
  Ipv6PktInfo ipv6_pktinfo;
} SendAndRecvCmsgSet;

typedef struct RecvCmsgSet {
  bool has_timestamp_nanos;
  int64_t timestamp_nanos;

  bool has_ip_tos;
  uint8_t ip_tos;

  bool has_ipv6_tclass;
  uint8_t ipv6_tclass;

  SendAndRecvCmsgSet send_and_recv;
} RecvCmsgSet;

typedef struct DeserializeSendMsgMetaResult {
  DeserializeSendMsgMetaError err;
  bool has_addr;
  IpAddress addr;
  uint16_t port;
  uint64_t zone_index;
  SendAndRecvCmsgSet cmsg_set;
} DeserializeSendMsgMetaResult;

// Utility for deserializing a SendMsgMeta from a provided buffer of bytes
// using the LLCPP bindings.
//
// Returns a `DeserializeSendMsgMetaResult` exposing metadata from the SendMsgMeta.
// On success, the `err` field of the returned result will be set to
// `DeserializeSendMsgMetaErrorNone`. On failure, it will be set to an error
// describing the reason for the failure.
UDP_SERDE_EXPORT DeserializeSendMsgMetaResult deserialize_send_msg_meta(Buffer buf);

typedef struct RecvMsgMeta {
  RecvCmsgSet cmsg_set;
  IpAddrType addr_type;
  uint16_t payload_size;
  uint16_t port;
  uint64_t zone_index;
} RecvMsgMeta;

typedef enum SerializeRecvMsgMetaError {
  SerializeRecvMsgMetaErrorNone,
  SerializeRecvMsgMetaErrorOutputBufferNull,
  SerializeRecvMsgMetaErrorOutputBufferTooSmall,
  SerializeRecvMsgMetaErrorFromAddrBufferNull,
  SerializeRecvMsgMetaErrorFromAddrBufferTooSmall,
  SerializeRecvMsgMetaErrorFailedToEncode,
} SerializeRecvMsgMetaError;

// Utility for serializing a RecvMsgMeta into the provided `out_buf` based on the
// metadata provided in `meta` and `addr`.
//
// On success, returns SerializeRecvMsgMetaErrorNone. On failure, returns an error
// describing the reason for the failure.
UDP_SERDE_EXPORT SerializeRecvMsgMetaError serialize_recv_msg_meta(const RecvMsgMeta* meta_,
                                                                   ConstBuffer addr,
                                                                   Buffer out_buf);

typedef struct SendMsgMeta {
  SendAndRecvCmsgSet cmsg_set;
  IpAddrType addr_type;
  uint16_t port;
  uint64_t zone_index;
} SendMsgMeta;

typedef enum SerializeSendMsgMetaError {
  SerializeSendMsgMetaErrorNone,
  SerializeSendMsgMetaErrorFailedToEncode,
  SerializeSendMsgMetaErrorOutputBufferNull,
  SerializeSendMsgMetaErrorOutputBufferTooSmall,
  SerializeSendMsgMetaErrorAddrBufferNull,
  SerializeSendMsgMetaErrorAddrBufferSizeMismatch,
} SerializeSendMsgMetaError;

// Utility for serializing a SendMsgMeta into the provided buffer
// using the LLCPP bindings.
//
// On success, returns SerializeSendMsgMetaErrorNone. On failure, returns an error
// describing the reason for the failure.
//
// This method is only intended to be used in tests.
// TODO(https://fxbug.dev/107864): Isolate testonly methods.
UDP_SERDE_EXPORT SerializeSendMsgMetaError serialize_send_msg_meta(const SendMsgMeta* meta_,
                                                                   ConstBuffer addr,
                                                                   Buffer out_buf);

typedef enum DeserializeRecvMsgMetaError {
  DeserializeRecvMsgMetaErrorNone,
  DeserializeRecvMsgMetaErrorInputBufferNull,
  DeserializeRecvMsgMetaErrorUnspecifiedDecodingFailure,
} DeserializeRecvMsgMetaError;

typedef struct DeserializeRecvMsgMetaResult {
  DeserializeRecvMsgMetaError err;
  bool has_addr;
  IpAddress addr;
  uint16_t payload_size;
  uint16_t port;
  uint64_t zone_index;
  RecvCmsgSet cmsg_set;
} DeserializeRecvMsgMetaResult;

// Utility for deserializing a fposix_socket::wire::RecvMsgMeta from a provided buffer
// of bytes using the LLCPP bindings.
//
// Returns a `DeserializeRecvMsgMetaResult` exposing metadata from the RecvMsgMeta.
// On success, the `err` field of the returned result will be set to
// `DeserializeRecvMsgMetaErrorNone`. On failure, it will be set to an error
// describing the reason for the failure.
//
// This method is only intended to be used in tests.
// TODO(https://fxbug.dev/107864): Isolate testonly methods.
UDP_SERDE_EXPORT DeserializeRecvMsgMetaResult deserialize_recv_msg_meta(Buffer buf);

// The length of the prelude bytes in a Tx message.
UDP_SERDE_EXPORT extern const uint32_t kTxUdpPreludeSize;

// The length of the prelude bytes in an Rx message.
UDP_SERDE_EXPORT extern const uint32_t kRxUdpPreludeSize;

#ifdef __cplusplus
}

#include <fidl/fuchsia.posix.socket/cpp/wire.h>
#include <lib/stdcompat/span.h>

namespace fsocket = fuchsia_posix_socket;

// Utility for serializing a fsocket::wire::SendMsgMeta into the provided buffer
// using the LLCPP bindings.
//
// On success, returns SerializeSendMsgMetaErrorNone. On failure, returns an error
// describing the reason for the failure.
UDP_SERDE_EXPORT SerializeSendMsgMetaError serialize_send_msg_meta(fsocket::wire::SendMsgMeta& meta,
                                                                   cpp20::span<uint8_t> out_buf);

// Utility for deserializing a RecvMsgMeta from the provided buffer.
//
// Returns a DecodedMessage<RecvMsgPayload>. On success, the DecodedMessage will
// be have `ok() == true`. On failure, the DecodedMessage will have `ok() == false`.
UDP_SERDE_EXPORT fidl::unstable::DecodedMessage<fsocket::wire::RecvMsgMeta>
deserialize_recv_msg_meta(cpp20::span<uint8_t> buf);

#endif  // __cplusplus

#endif  // SRC_CONNECTIVITY_NETWORK_NETSTACK_UDP_SERDE_UDP_SERDE_H_
