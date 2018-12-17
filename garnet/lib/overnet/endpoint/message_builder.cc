// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/overnet/endpoint/message_builder.h"
#include "garnet/lib/overnet/protocol/fidl.h"
namespace overnet {

namespace {
static const uint8_t kFirstRequiredParse = 1;
static const uint8_t kFirstSkippableParse = 65;
static const uint8_t kFirstHandle = 128;

enum class MessageFragmentType : uint8_t {
  kTxId = 1,
  kOrdinal = 2,
  kBody = 127,
  kChannel = 128,
};
}  // namespace

StatusOr<RouterEndpoint::NewStream> MessageWireEncoder::AppendChannelHandle(
    fuchsia::overnet::protocol::Introduction introduction) {
  auto fork_status = stream_->Fork(
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered,
      std::move(introduction));
  if (fork_status.is_error()) {
    return fork_status.AsStatus();
  }
  auto fork_frame = std::move(*Encode(&fork_status->fork_frame));
  auto fork_frame_len = fork_frame.length();
  auto fork_frame_len_len = varint::WireSizeFor(fork_frame_len);
  tail_.emplace_back(Slice::WithInitializer(
      1 + fork_frame_len_len + fork_frame_len, [&](uint8_t* p) {
        *p++ = static_cast<uint8_t>(MessageFragmentType::kChannel);
        p = varint::Write(fork_frame_len, fork_frame_len_len, p);
        memcpy(p, fork_frame.begin(), fork_frame_len);
      }));
  return std::move(fork_status->new_stream);
}

Slice MessageWireEncoder::Write(Border desired_border) const {
  // Wire format:
  // (1-byte fragment id, fragment length varint, fragment data)*
  // fragment id's must be in ordinal order (except for handles, which must be
  // last and be placed in the order which they should be appended)

  const auto txid_len = txid_ ? varint::WireSizeFor(txid_) : 0;
  const auto txid_len_len = txid_ ? varint::WireSizeFor(txid_len) : 0;
  assert(ordinal_ != 0);
  const auto ordinal_len = varint::WireSizeFor(ordinal_);
  const auto ordinal_len_len = varint::WireSizeFor(ordinal_len);
  const auto body_len = body_.length();
  const auto body_len_len = varint::WireSizeFor(body_len);

  const auto message_length_without_tail =
      // space for txid
      (txid_ ? 1 + txid_len_len + txid_len : 0) +
      // space for ordinal
      1 + ordinal_len_len + ordinal_len +
      // space for body
      1 + body_len_len + body_len;

  return Slice::Join(
             tail_.begin(), tail_.end(),
             desired_border.WithAddedPrefix(message_length_without_tail))
      .WithPrefix(message_length_without_tail, [&](uint8_t* const data) {
        uint8_t* p = data;
        if (txid_) {
          *p++ = static_cast<uint8_t>(MessageFragmentType::kTxId);
          p = varint::Write(txid_len, txid_len_len, p);
          p = varint::Write(txid_, txid_len, p);
        }
        *p++ = static_cast<uint8_t>(MessageFragmentType::kOrdinal);
        p = varint::Write(ordinal_len, ordinal_len_len, p);
        p = varint::Write(ordinal_, ordinal_len, p);
        *p++ = static_cast<uint8_t>(MessageFragmentType::kBody);
        p = varint::Write(body_len, body_len_len, p);
        memcpy(p, body_.begin(), body_len);
        p += body_len;
        assert(p == data + message_length_without_tail);
      });
}

Status ParseMessageInto(Slice slice, NodeId peer,
                        RouterEndpoint* router_endpoint,
                        MessageReceiver* builder) {
  const uint8_t* p = slice.begin();
  const uint8_t* end = slice.end();

  MessageFragmentType largest_fragment_id_seen =
      static_cast<MessageFragmentType>(0);

  while (p != end) {
    const MessageFragmentType fragment_type =
        static_cast<MessageFragmentType>(*p++);
    if (fragment_type >= static_cast<MessageFragmentType>(kFirstHandle)) {
      largest_fragment_id_seen = static_cast<MessageFragmentType>(kFirstHandle);
    } else {
      if (fragment_type <= largest_fragment_id_seen) {
        return Status(StatusCode::FAILED_PRECONDITION,
                      "Message fragments must be written in ascending order of "
                      "fragment ordinal");
      }
      largest_fragment_id_seen = fragment_type;
    }
    uint64_t fragment_length;
    if (!varint::Read(&p, end, &fragment_length)) {
      return Status(StatusCode::FAILED_PRECONDITION,
                    "Failed to read message fragment length");
    }
    if (fragment_length > uint64_t(p - end)) {
      return Status(StatusCode::FAILED_PRECONDITION,
                    "Fragment length is longer than total message");
    }
    const uint8_t* next_fragment = p + fragment_length;
    switch (fragment_type) {
      case MessageFragmentType::kTxId: {
        uint64_t txid;
        if (!varint::Read(&p, end, &txid)) {
          return Status(StatusCode::FAILED_PRECONDITION,
                        "Failed to parse txid");
        }
        if (txid >= 0x80000000u) {
          return Status(StatusCode::FAILED_PRECONDITION, "Txid out of range");
        }
        auto set_status = builder->SetTransactionId(txid);
        if (set_status.is_error()) {
          return set_status;
        }
      } break;
      case MessageFragmentType::kOrdinal: {
        uint64_t ordinal;
        if (!varint::Read(&p, end, &ordinal)) {
          return Status(StatusCode::FAILED_PRECONDITION,
                        "Failed to parse ordinal");
        }
        if (ordinal > std::numeric_limits<uint32_t>::max() || ordinal == 0) {
          return Status(StatusCode::FAILED_PRECONDITION,
                        "Ordinal out of range");
        }
        auto set_status = builder->SetOrdinal(ordinal);
        if (set_status.is_error()) {
          return set_status;
        }
      } break;
      case MessageFragmentType::kBody: {
        auto set_status =
            builder->SetBody(slice.FromPointer(p).ToOffset(fragment_length));
        if (set_status.is_error()) {
          return set_status;
        }
        p = next_fragment;
      } break;
      case MessageFragmentType::kChannel: {
        auto fork_frame = Decode<fuchsia::overnet::protocol::ForkFrame>(
            slice.FromPointer(p).ToOffset(fragment_length));
        if (fork_frame.is_error()) {
          return fork_frame.AsStatus();
        }
        auto intro =
            router_endpoint->UnwrapForkFrame(peer, std::move(*fork_frame));
        if (intro.is_error()) {
          return intro.AsStatus();
        }
        auto append_status = builder->AppendChannelHandle(std::move(*intro));
        if (append_status.is_error()) {
          return append_status;
        }
        p = next_fragment;
      } break;
      default: {
        const uint8_t type_byte = static_cast<uint8_t>(fragment_type);
        if (type_byte >= kFirstRequiredParse &&
            type_byte < kFirstSkippableParse) {
          return Status(StatusCode::FAILED_PRECONDITION,
                        "Failed to parse a fragment that is required: "
                        "version mismatch?");
        } else {
          if (type_byte >= kFirstHandle) {
            auto set_status = builder->AppendUnknownHandle();
            if (set_status.is_error()) {
              return set_status;
            }
          }
          p = next_fragment;
        }
      } break;
    }
    assert(p == next_fragment);
    p = next_fragment;
  }
  return Status::Ok();
}

}  // namespace overnet
