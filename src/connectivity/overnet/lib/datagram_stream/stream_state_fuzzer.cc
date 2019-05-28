// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "src/connectivity/overnet/lib/datagram_stream/stream_state.h"

using namespace overnet;

namespace {

bool logging = false;

#define FUZZLOG \
  if (!logging) \
    ;           \
  else          \
    std::cout

class InputStream {
 public:
  InputStream(const uint8_t* data, size_t size)
      : cur_(data), end_(data + size) {}

  uint8_t NextByte() {
    if (cur_ == end_)
      return 0;
    return *cur_++;
  }

  bool NextBool() { return (NextByte() & 1) != 0; }

  std::string NextString() {
    int length = NextByte();
    std::string out;
    for (int i = 0; i < length; i++) {
      out.push_back(NextByte());
    }
    return out;
  }

  Status NextStatus() {
    auto code = static_cast<StatusCode>(NextByte());
    if (NextBool()) {
      auto reason = NextString();
      return Status(code, reason);
    } else {
      return Status(code);
    }
  }

 private:
  const uint8_t* cur_;
  const uint8_t* end_;
};

class Fuzzer : private StreamStateListener {
 public:
  StreamState state{this};
  bool stopped_reading = false;
  bool stream_closed = false;
  int quiesce_functions_made = 0;
  int quiesce_functions_called = 0;

  auto MakeQuiesceFunction() {
    quiesce_functions_made++;
    return [this] { quiesce_functions_called++; };
  }

  bool TakeAck() {
    if (can_ack) {
      can_ack = false;
      return true;
    }
    return false;
  }

  void CheckDescription() {
    if (logging) {
      std::cout << "state=" << state.Description() << '\n';
    } else {
      state.Description();
    }
  }

  void ValidateState() {
    CheckDescription();

    if (state.CanBeginSend()) {
      assert(state.IsOpenForSending());
    }
    if (state.IsOpenForSending()) {
      assert(state.CanBeginOp());
    }
    if (state.IsOpenForReceiving()) {
      assert(state.CanBeginOp());
    }
    if (state.IsOpenForSending() || state.IsOpenForReceiving()) {
      assert(!stream_closed);
    }
    if (state.IsClosedForReceiving()) {
      assert(stopped_reading);
    } else {
      assert(!stopped_reading);
    }
  }

 private:
  bool can_ack = false;
  int close_retries;
  Optional<Status> last_sent_close;

  void SendClose() {
    FUZZLOG << "--> SendClose()\n";
    CheckDescription();

    assert(!can_ack);
    auto status = state.GetSendStatus();
    can_ack = true;

    if (status.code() != last_sent_close.Map([](const Status& status) {
          return status.code();
        })) {
      close_retries = 0;
      last_sent_close = status;
    } else {
      close_retries++;
      assert(close_retries <= StreamState::kMaxCloseRetries);
    }
  }
  void StopReading(const Status& status) {
    FUZZLOG << "--> StopReading(" << status << ")\n";
    CheckDescription();

    assert(!stopped_reading);
    stopped_reading = true;
    assert(state.IsClosedForReceiving());
  }
  void StreamClosed() {
    FUZZLOG << "--> StreamClosed()\n";
    CheckDescription();

    assert(stopped_reading);
    assert(!stream_closed);
    stream_closed = true;
    assert(state.IsClosedForReceiving());
    assert(state.IsClosedForSending());
  }
};
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  InputStream input(data, size);
  Fuzzer fuzzer;
  int ops_begun = 0;
  int sends_begun = 0;
  bool quiesce_ready_called = false;

  while (fuzzer.quiesce_functions_called < 1) {
    FUZZLOG << "------\n";
    fuzzer.ValidateState();

    switch (input.NextByte()) {
      case 1: {
        auto status = input.NextStatus();
        FUZZLOG << "LocalClose(" << status << ")\n";
        fuzzer.state.LocalClose(status, fuzzer.MakeQuiesceFunction());
      } break;
      case 2: {
        auto status = input.NextStatus();
        FUZZLOG << "RemoteClose(" << status << ")\n";
        fuzzer.state.RemoteClose(status);
      } break;
      case 3: {
        if (fuzzer.TakeAck()) {
          auto status = input.NextStatus();
          FUZZLOG << "SendCloseAck(" << status << ")\n";
          fuzzer.state.SendCloseAck(status);
        }
      } break;
      case 4: {
        if (fuzzer.state.CanBeginOp()) {
          FUZZLOG << "BeginOp()\n";
          ops_begun++;
          fuzzer.state.BeginOp();
        }
      } break;
      case 5:
        if (ops_begun) {
          FUZZLOG << "EndOp()\n";
          ops_begun--;
          fuzzer.state.EndOp();
        }
        break;
      case 6:
        if (fuzzer.stream_closed && !quiesce_ready_called) {
          FUZZLOG << "QuiesceReady()\n";
          quiesce_ready_called = true;
          fuzzer.state.QuiesceReady();
        }
        break;
      case 7: {
        if (fuzzer.state.CanBeginSend()) {
          FUZZLOG << "BeginSend()\n";
          sends_begun++;
          fuzzer.state.BeginSend();
        }
      } break;
      case 8:
        if (sends_begun) {
          FUZZLOG << "EndSend()\n";
          sends_begun--;
          fuzzer.state.EndSend();
        }
        break;
      default:
        FUZZLOG << "Cleanup()\n";
        FUZZLOG << "  LocalClose(Ok)\n";
        fuzzer.state.LocalClose(Status::Ok(), fuzzer.MakeQuiesceFunction());
        FUZZLOG << "  state --> " << fuzzer.state.Description() << "\n";
        FUZZLOG << "  RemoteClose(Cancelled)\n";
        fuzzer.state.RemoteClose(Status::Cancelled());
        FUZZLOG << "  state --> " << fuzzer.state.Description() << "\n";
        for (int i = 0; i < sends_begun; i++) {
          FUZZLOG << "  EndSend()\n";
          fuzzer.state.EndSend();
          FUZZLOG << "  state --> " << fuzzer.state.Description() << "\n";
        }
        for (int i = 0; i < ops_begun; i++) {
          FUZZLOG << "  EndOp()\n";
          fuzzer.state.EndOp();
          FUZZLOG << "  state --> " << fuzzer.state.Description() << "\n";
        }
        ops_begun = 0;
        if (fuzzer.TakeAck()) {
          FUZZLOG << "  SendCloseAck(Cancelled)\n";
          fuzzer.state.SendCloseAck(Status::Cancelled());
          FUZZLOG << "  state --> " << fuzzer.state.Description() << "\n";
        }
        assert(fuzzer.stream_closed);
        if (!quiesce_ready_called) {
          FUZZLOG << "  QuiesceReady()\n";
          quiesce_ready_called = true;
          fuzzer.state.QuiesceReady();
          FUZZLOG << "  state --> " << fuzzer.state.Description() << "\n";
        }
        break;
    }
  }

  FUZZLOG << "------\n";
  FUZZLOG << "Done()\n";
  fuzzer.ValidateState();
  assert(quiesce_ready_called);
  assert(ops_begun == 0);
  assert(fuzzer.quiesce_functions_called == fuzzer.quiesce_functions_made);

  return 0;
}
