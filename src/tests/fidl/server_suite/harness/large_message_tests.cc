// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/fidl.serversuite/cpp/natural_types.h"
#include "lib/fidl/cpp/unified_messaging.h"
#include "src/tests/fidl/server_suite/harness/harness.h"
#include "src/tests/fidl/server_suite/harness/ordinals.h"

using namespace channel_util;
using namespace fidl_serversuite;

namespace server_suite {

namespace {

const uint32_t kVectorEnvelopeSize = 16;

Bytes populate_unset_handles_false() { return u64(0); }

// An encode test has three interesting properties that we want to validate: the attached handle
// state, the bytes in the channel message itself, and the existence and contents of the overflow
// buffer that may or may not be attached. Every "good" test case involving the
// |UnboundedMaybeLargeResource| FIDL type will need to be checked against this struct.
struct Expected {
  HandleInfos handle_infos;
  Bytes channel_bytes;
  std::optional<Bytes> vmo_bytes;
};

// Because |UnboundedMaybeLargeResource| is used so widely, and needs to have many parts (handles,
// VMO-stored data, etc) assembled just so in a variety of configurations (small/large with 0, 63,
// or 64 handles, plus all manner of mis-encodings), this helper struct keeps track of all of the
// bookkeeping necessary when building an |UnboundedMaybeLargeResource| of a certain shape.
class UnboundedMaybeLargeResourceWriter {
 public:
  enum class HandlePresence {
    kAbsent = 0,
    kPresent = 1,
  };

  using ByteVectorSize = size_t;
  UnboundedMaybeLargeResourceWriter(UnboundedMaybeLargeResourceWriter&&) = default;
  UnboundedMaybeLargeResourceWriter& operator=(UnboundedMaybeLargeResourceWriter&&) = default;
  UnboundedMaybeLargeResourceWriter(const UnboundedMaybeLargeResourceWriter&) = default;
  UnboundedMaybeLargeResourceWriter& operator=(const UnboundedMaybeLargeResourceWriter&) = default;

  // The first argument, |num_filled|, is a pair that specifies the number of entries in the
  // |elements| array that should be set to non-empty vectors, with the other number in the pair
  // specifying the number of bytes in each such vector. The second argument, |num_handles|,
  // specifies the number of |elements| that should have a present handle. For instance, the
  // constructor call |UnboundedMaybeLargeResourceWriter({30, 1000}, 20)| would produce an
  // `elements` array whose first 20 entries have 1000 bytes and a handle, 10 more entries that have
  // absent byte vectors and a handle, with the final 34 entries containing both absent byte vectors
  // and absent handles.
  //
  // Generally speaking, tests will be clearer and more readable if users create a descriptively
  // named static builder on this class for their specific case (ex:
  // |LargestSmallMessage64Handles|).
  UnboundedMaybeLargeResourceWriter(std::pair<size_t, ByteVectorSize> num_filled,
                                    uint8_t num_handles) {
    for (size_t i = 0; i < kHandleCarryingElementsCount; i++) {
      byte_vector_sizes[i] = 0;
      handles[i] = HandlePresence::kAbsent;
      if (i < num_filled.first) {
        byte_vector_sizes[i] = num_filled.second;
        if (num_handles) {
          handles[i] = HandlePresence::kPresent;
        }
      }
    }
  }

  static UnboundedMaybeLargeResourceWriter LargestSmallMessage64Handles() {
    static const uint32_t kDefaultElementsCount = kHandleCarryingElementsCount - 1;
    auto writer = UnboundedMaybeLargeResourceWriter(
        {kDefaultElementsCount, kFirst63ElementsByteVectorSize}, kHandleCarryingElementsCount);
    writer.byte_vector_sizes[kDefaultElementsCount] = kSmallLastElementByteVectorSize;
    return writer;
  }

  void WriteSmallMessageForDecode(channel_util::Channel& client, Bytes header) {
    WriteSmallMessage(client, header);
  }

  void WriteSmallMessageForEncode(channel_util::Channel& client, Bytes header,
                                  Bytes populate_unset_handles, Expected* out_expected) {
    WriteSmallMessage(client, header, populate_unset_handles, out_expected);
  }

 private:
  Bytes BuildPayload() {
    std::vector<Bytes> payload_bytes;
    // Add the inline portions of each |element| entry.
    for (size_t i = 0; i < kHandleCarryingElementsCount; i++) {
      payload_bytes.push_back(
          {vector_header(byte_vector_sizes[i]),
           handles[i] == HandlePresence::kPresent ? handle_present() : handle_absent(),
           padding(4)});
    }
    // Now do all out of line portions as well.
    for (size_t i = 0; i < kHandleCarryingElementsCount; i++) {
      if (byte_vector_sizes[i] > 0) {
        payload_bytes.push_back(repeat(kSomeByte).times(byte_vector_sizes[i]));
      }
    }
    return Bytes(payload_bytes);
  }

  HandleDispositions BuildHandleDispositions() {
    HandleDispositions dispositions;
    for (const auto& maybe_handle : handles) {
      if (maybe_handle == HandlePresence::kPresent) {
        zx::event event;
        zx::event::create(0, &event);
        dispositions.push_back(zx_handle_disposition_t{
            .handle = event.release(),
            .type = ZX_OBJ_TYPE_EVENT,
            .rights = ZX_DEFAULT_EVENT_RIGHTS,
        });
      }
    }
    return dispositions;
  }

  HandleInfos BuildHandleInfos() {
    HandleInfos infos;
    for (const auto& maybe_handle : handles) {
      if (maybe_handle == HandlePresence::kPresent) {
        infos.push_back(zx_handle_info_t{
            .type = ZX_OBJ_TYPE_EVENT,
            .rights = ZX_DEFAULT_EVENT_RIGHTS,
        });
      }
    }
    return infos;
  }

  void WriteSmallMessage(channel_util::Channel& client, Bytes header,
                         Bytes populate_unset_handles = Bytes(), Expected* out_expected = nullptr) {
    Bytes payload = BuildPayload();
    ZX_ASSERT_MSG(sizeof(fidl_message_header_t) + payload.size() <= ZX_CHANNEL_MAX_MSG_BYTES,
                  "attempted to write large message using small message writer");

    if (out_expected != nullptr) {
      *out_expected = Expected{
          .handle_infos = BuildHandleInfos(),
          .channel_bytes = Bytes({header, payload}),
          .vmo_bytes = std::nullopt,
      };
    }
    Bytes bytes_in = Bytes({header, populate_unset_handles, payload});

    ASSERT_OK(client.write(bytes_in, BuildHandleDispositions()));
  }

  std::array<ByteVectorSize, kHandleCarryingElementsCount> byte_vector_sizes;
  std::array<HandlePresence, kHandleCarryingElementsCount> handles;
};
}  // namespace

// ////////////////////////////////////////////////////////////////////////
// Good decode tests
// ////////////////////////////////////////////////////////////////////////

void GoodDecodeSmallStructOfByteVector(ServerTest* testing, uint64_t method_ordinal) {
  uint32_t n = kSmallStructByteVectorSize;
  Bytes bytes_in = {
      header(kOneWayTxid, method_ordinal, fidl::MessageDynamicFlags::kStrictMethod),
      vector_header(n),
      repeat(kSomeByte).times(n),
  };

  ASSERT_OK(testing->client_end().write(bytes_in));
  WAIT_UNTIL_EXT(testing, [&]() { return testing->reporter().received_strict_one_way(); });
}

void GoodDecodeSmallUnionOfByteVector(ServerTest* testing, uint64_t method_ordinal) {
  uint32_t n = kSmallUnionByteVectorSize;
  Bytes bytes_in = {
      header(kOneWayTxid, method_ordinal, fidl::MessageDynamicFlags::kStrictMethod),
      union_ordinal(1),
      out_of_line_envelope(n + kVectorEnvelopeSize, 0),
      vector_header(n),
      repeat(kSomeByte).times(n),
  };

  ASSERT_OK(testing->client_end().write(bytes_in));
  WAIT_UNTIL_EXT(testing, [&]() { return testing->reporter().received_strict_one_way(); });
}

LARGE_MESSAGE_SERVER_TEST(GoodDecodeBoundedKnownSmallMessage) {
  GoodDecodeSmallStructOfByteVector(this, kDecodeBoundedKnownToBeSmall);
}

LARGE_MESSAGE_SERVER_TEST(GoodDecodeBoundedMaybeSmallMessage) {
  GoodDecodeSmallStructOfByteVector(this, kDecodeBoundedMaybeLarge);
}

LARGE_MESSAGE_SERVER_TEST(GoodDecodeSemiBoundedUnknowableSmallMessage) {
  GoodDecodeSmallUnionOfByteVector(this, kDecodeSemiBoundedBelievedToBeSmall);
}

LARGE_MESSAGE_SERVER_TEST(GoodDecodeSemiBoundedMaybeSmallMessage) {
  GoodDecodeSmallUnionOfByteVector(this, kDecodeSemiBoundedMaybeLarge);
}

LARGE_MESSAGE_SERVER_TEST(GoodDecodeUnboundedSmallMessage) {
  GoodDecodeSmallStructOfByteVector(this, kDecodeUnboundedMaybeLargeValue);
}

LARGE_MESSAGE_SERVER_TEST(GoodDecode64HandleSmallMessage) {
  auto writer = UnboundedMaybeLargeResourceWriter::LargestSmallMessage64Handles();
  writer.WriteSmallMessageForDecode(client_end(),
                                    header(kOneWayTxid, kDecodeUnboundedMaybeLargeResource,
                                           fidl::MessageDynamicFlags::kStrictMethod));

  WAIT_UNTIL([this]() { return reporter().received_strict_one_way(); });
}

LARGE_MESSAGE_SERVER_TEST(GoodDecodeUnknownSmallMessage) {
  uint32_t n = kSmallStructByteVectorSize;
  Bytes bytes_in = {
      header(kOneWayTxid, kOrdinalFakeUnknownMethod, fidl::MessageDynamicFlags::kFlexibleMethod),
      vector_header(n),
      repeat(kSomeByte).times(n),
  };

  ASSERT_OK(client_end().write(bytes_in));
  WAIT_UNTIL([this]() { return reporter().received_unknown_method().has_value(); });
}

// ////////////////////////////////////////////////////////////////////////
// Good encode tests
// ////////////////////////////////////////////////////////////////////////

void GoodEncodeSmallStructOfByteVector(ServerTest* testing, uint64_t method_ordinal) {
  uint32_t n = kSmallStructByteVectorSize;
  Bytes bytes_out = {
      header(kTwoWayTxid, method_ordinal, fidl::MessageDynamicFlags::kStrictMethod),
      vector_header(n),
      repeat(kSomeByte).times(n),
  };
  Bytes bytes_in(bytes_out);

  ASSERT_OK(testing->client_end().write(bytes_in));
  ASSERT_OK(testing->client_end().wait_for_signal(ZX_CHANNEL_READABLE));
  ASSERT_OK(testing->client_end().read_and_check(bytes_out));
}

void GoodEncodeSmallUnionOfByteVector(ServerTest* testing, uint64_t method_ordinal) {
  uint32_t n = kSmallUnionByteVectorSize;
  Bytes bytes_out = {
      header(kTwoWayTxid, method_ordinal, fidl::MessageDynamicFlags::kStrictMethod),
      union_ordinal(1),
      out_of_line_envelope(n + kVectorEnvelopeSize, 0),
      vector_header(n),
      repeat(kSomeByte).times(n),
  };
  Bytes bytes_in(bytes_out);

  ASSERT_OK(testing->client_end().write(bytes_in));
  ASSERT_OK(testing->client_end().wait_for_signal(ZX_CHANNEL_READABLE));
  ASSERT_OK(testing->client_end().read_and_check(bytes_out));
}

LARGE_MESSAGE_SERVER_TEST(GoodEncodeBoundedKnownSmallMessage) {
  GoodEncodeSmallStructOfByteVector(this, kEncodeBoundedKnownToBeSmall);
}

LARGE_MESSAGE_SERVER_TEST(GoodEncodeBoundedMaybeSmallMessage) {
  GoodEncodeSmallStructOfByteVector(this, kEncodeBoundedMaybeLarge);
}

LARGE_MESSAGE_SERVER_TEST(GoodEncodeSemiBoundedKnownSmallMessage) {
  GoodEncodeSmallUnionOfByteVector(this, kEncodeSemiBoundedBelievedToBeSmall);
}

LARGE_MESSAGE_SERVER_TEST(GoodEncodeSemiBoundedMaybeSmallMessage) {
  GoodEncodeSmallUnionOfByteVector(this, kEncodeSemiBoundedMaybeLarge);
}

LARGE_MESSAGE_SERVER_TEST(GoodEncodeUnboundedSmallMessage) {
  GoodEncodeSmallStructOfByteVector(this, kEncodeUnboundedMaybeLargeValue);
}

LARGE_MESSAGE_SERVER_TEST(GoodEncode64HandleSmallMessage) {
  auto writer = UnboundedMaybeLargeResourceWriter::LargestSmallMessage64Handles();
  Expected expect;
  writer.WriteSmallMessageForEncode(client_end(),
                                    header(kTwoWayTxid, kEncodeUnboundedMaybeLargeResource,
                                           fidl::MessageDynamicFlags::kStrictMethod),
                                    populate_unset_handles_false(), &expect);

  ASSERT_OK(client_end().wait_for_signal(ZX_CHANNEL_READABLE));
  ASSERT_OK(client_end().read_and_check(expect.channel_bytes, expect.handle_infos));
}

// TODO(fxbug.dev/114259): Write remaining tests for encoding/decoding large messages.

}  // namespace server_suite
