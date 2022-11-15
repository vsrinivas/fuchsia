// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.posix.socket/cpp/wire.h>
#include <lib/stdcompat/span.h>
#include <lib/syslog/cpp/macros.h>

#include <perftest/perftest.h>

#include "src/connectivity/network/netstack/udp_serde/udp_serde.h"
#include "src/connectivity/network/netstack/udp_serde/udp_serde_test_util.h"
#include "src/lib/fxl/strings/string_printf.h"

// Measure the times to serialize and deserialize the SendMsgMeta and RecvMsgMeta FIDL messages
// used as part of the Fast UDP protocol. Each operation is benchmarked against messages that
// are 1) maximally filled with data and 2) minimally filled with data.

namespace {

enum class OpSequence { Serialize, Deserialize, SerializeThenDeserialize };

bool SendMsgMeta(perftest::RepeatState* const state, AddrKind::Kind addr_kind, bool with_data,
                 OpSequence op_sequence) {
  TestSendMsgMeta meta(addr_kind);
  fidl::Arena alloc;
  fsocket::wire::SendMsgMeta fidl = meta.GetFidl(alloc, with_data);

  uint8_t data[kTxUdpPreludeSize];
  const Buffer buf = {
      .buf = data,
      .buf_size = kTxUdpPreludeSize,
  };
  const cpp20::span<uint8_t> span(data, kTxUdpPreludeSize);

  auto validate_serialize = [&fidl, &span]() {
    perftest::DoNotOptimize(&fidl);
    perftest::DoNotOptimize(&span);
    SerializeSendMsgMetaError result = serialize_send_msg_meta(fidl, span);
    perftest::DoNotOptimize(result);
    FX_CHECK(result == SerializeSendMsgMetaErrorNone);
  };

  auto validate_deserialize = [](const Buffer& buf) {
    perftest::DoNotOptimize(&buf);
    DeserializeSendMsgMetaResult deserialize_result = deserialize_send_msg_meta(buf);
    perftest::DoNotOptimize(deserialize_result);
    FX_CHECK(deserialize_result.err == DeserializeSendMsgMetaErrorNone);
  };

  switch (op_sequence) {
    case OpSequence::Serialize: {
      while (state->KeepRunning()) {
        validate_serialize();
      }
    } break;
    case OpSequence::Deserialize: {
      validate_serialize();

      uint8_t data_copy[kTxUdpPreludeSize];
      const Buffer buf_copy = {.buf = data_copy, .buf_size = kTxUdpPreludeSize};

      while (state->KeepRunning()) {
        memcpy(data_copy, data, kTxUdpPreludeSize);
        validate_deserialize(buf_copy);
      }
      break;
    }
    case OpSequence::SerializeThenDeserialize:
      while (state->KeepRunning()) {
        validate_serialize();
        validate_deserialize(buf);
      }
      break;
  }

  return true;
}

bool RecvMsgMeta(perftest::RepeatState* const state, AddrKind::Kind addr_kind, bool with_data,
                 OpSequence op_sequence) {
  TestRecvMsgMeta test_meta(addr_kind);
  const auto& [meta, addr_buf] = test_meta.GetSerializeInput(with_data);

  uint8_t data[kRxUdpPreludeSize];
  const Buffer buf = {.buf = data, .buf_size = kRxUdpPreludeSize};
  const cpp20::span<uint8_t> span(data, kRxUdpPreludeSize);

  auto validate_serialize = [&buf](const struct RecvMsgMeta& meta, const ConstBuffer& addr_buf) {
    perftest::DoNotOptimize(&meta);
    perftest::DoNotOptimize(&buf);
    perftest::DoNotOptimize(&addr_buf);

    SerializeRecvMsgMetaError result = serialize_recv_msg_meta(&meta, addr_buf, buf);
    perftest::DoNotOptimize(result);
    FX_CHECK(result == SerializeRecvMsgMetaErrorNone);
  };

  auto validate_deserialize = [](const cpp20::span<uint8_t>& span) {
    perftest::DoNotOptimize(&span);
    fit::result decoded = deserialize_recv_msg_meta(span);
    perftest::DoNotOptimize(decoded);
    FX_CHECK(decoded.is_ok());
  };

  switch (op_sequence) {
    case OpSequence::Serialize:
      while (state->KeepRunning()) {
        validate_serialize(meta, addr_buf);
      }
      break;
    case OpSequence::Deserialize: {
      validate_serialize(meta, addr_buf);

      uint8_t data_copy[kRxUdpPreludeSize];
      const cpp20::span<uint8_t> span_copy(data_copy, kRxUdpPreludeSize);
      while (state->KeepRunning()) {
        memcpy(data_copy, data, kRxUdpPreludeSize);
        validate_deserialize(span_copy);
      }
    } break;
    case OpSequence::SerializeThenDeserialize:
      while (state->KeepRunning()) {
        validate_serialize(meta, addr_buf);
        validate_deserialize(span);
      }
      break;
  }

  return true;
}

void RegisterTests() {
  enum class FidlMessage { SendMsgMeta, RecvMsgMeta };

  auto fidl_message_to_string = [](FidlMessage msg) {
    switch (msg) {
      case FidlMessage::SendMsgMeta:
        return "SendMsgMeta";
      case FidlMessage::RecvMsgMeta:
        return "RecvMsgMeta";
    }
  };

  auto with_data_to_string = [](bool with_data) {
    if (with_data) {
      return "Full";
    }
    return "Empty";
  };

  auto op_sequence_to_string = [](OpSequence op_sequence) {
    switch (op_sequence) {
      case OpSequence::Serialize:
        return "Serialize";
      case OpSequence::Deserialize:
        return "Deserialize";
      case OpSequence::SerializeThenDeserialize:
        return "SerializeThenDeserialize";
    }
  };

  auto get_test_name = [&fidl_message_to_string, &op_sequence_to_string, &with_data_to_string](
                           FidlMessage fidl, bool with_data, AddrKind::Kind addr_kind,
                           OpSequence op_sequence) {
    return fxl::StringPrintf("%s/%s/%s/%s", fidl_message_to_string(fidl),
                             op_sequence_to_string(op_sequence), AddrKind(addr_kind).ToString(),
                             with_data_to_string(with_data));
  };

  constexpr std::array<OpSequence, 3> kAllTestedOps = {
      OpSequence::Serialize, OpSequence::Deserialize, OpSequence::SerializeThenDeserialize};
  constexpr std::array<bool, 2> kAllDataConditions = {true, false};
  constexpr std::array<AddrKind::Kind, 2> kAllAddrKinds = {AddrKind::Kind::V4, AddrKind::Kind::V6};

  for (const OpSequence& op : kAllTestedOps) {
    for (const bool& with_data : kAllDataConditions) {
      for (const AddrKind::Kind& addr_kind : kAllAddrKinds) {
        perftest::RegisterTest(
            get_test_name(FidlMessage::SendMsgMeta, with_data, addr_kind, op).c_str(), SendMsgMeta,
            addr_kind, with_data, op);
        perftest::RegisterTest(
            get_test_name(FidlMessage::RecvMsgMeta, with_data, addr_kind, op).c_str(), RecvMsgMeta,
            addr_kind, with_data, op);
      }
    }
  }
}

PERFTEST_CTOR(RegisterTests)

}  // namespace

int main(int argc, char** argv) {
  return perftest::PerfTestMain(argc, argv, "fuchsia.network.udp_serde");
}
