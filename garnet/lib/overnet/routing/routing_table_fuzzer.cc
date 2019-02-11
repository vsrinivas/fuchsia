// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/overnet/protocol/fidl.h"
#include "garnet/lib/overnet/protocol/varint.h"
#include "garnet/lib/overnet/routing/routing_table.h"
#include "garnet/lib/overnet/testing/test_timer.h"
#include "garnet/lib/overnet/testing/trace_cout.h"
#include "garnet/public/lib/fostr/fidl/fuchsia/overnet/protocol/formatting.h"

using namespace overnet;

namespace {

class RoutingTableFuzzer {
 public:
  RoutingTableFuzzer(bool log_stuff)
      : logging_(log_stuff ? new Logging(&timer_) : nullptr) {}

  bool StepTime(uint64_t micros) {
    timer_.Step(micros);
    return timer_.Now().after_epoch() != TimeDelta::PositiveInf();
  }

  void ProcessUpdate(Slice update) {
    auto parse_status =
        Decode<fuchsia::overnet::protocol::RoutingTableUpdate>(update);
    OVERNET_TRACE(INFO) << "Parse: " << parse_status;
    if (parse_status.is_error()) {
      return;
    }
    auto nodes = std::move(*parse_status->mutable_nodes());
    auto links = std::move(*parse_status->mutable_links());
    auto validation_status =
        routing_table_.ValidateIncomingUpdate(nodes, links);
    OVERNET_TRACE(INFO) << "Validate: " << validation_status;
    if (validation_status.is_error()) {
      return;
    }
    routing_table_.ProcessUpdate(std::move(nodes), std::move(links), true);
  }

  void GenerateUpdate() { routing_table_.GenerateUpdate(NodeId(2)); }

 private:
  TestTimer timer_;
  struct Logging {
    Logging(Timer* timer) : tracer(timer) {}
    TraceCout tracer;
    ScopedRenderer set_tracer{&tracer};
  };
  std::unique_ptr<Logging> logging_;
  RoutingTable routing_table_{NodeId(1), &timer_, false};
};

class InputStream {
 public:
  InputStream(const uint8_t* data, size_t size)
      : cur_(data), end_(data + size) {}

  uint64_t Next64() {
    uint64_t out;
    if (!varint::Read(&cur_, end_, &out))
      out = 0;
    return out;
  }

  uint8_t NextByte() {
    if (cur_ == end_)
      return 0;
    return *cur_++;
  }

  Slice NextSlice() {
    auto len = std::min(uint64_t(1024 * 1024), Next64());
    return Slice::WithInitializerAndBorders(
        len, Border::None(), [this, len](uint8_t* p) {
          for (uint64_t i = 0; i < len; i++) {
            *p++ = NextByte();
          }
        });
  }

 private:
  const uint8_t* cur_;
  const uint8_t* end_;
};
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  InputStream input(data, size);
  RoutingTableFuzzer fuzzer(false);
  for (;;) {
    switch (input.NextByte()) {
      default:
        // input exhausted, or unknown op-code
        return 0;
      case 1:
        if (!fuzzer.StepTime(input.Next64()))
          return 0;
        break;
      case 2:
        // Ignores failures in order to verify that the next input doesn't
        // crash.
        fuzzer.ProcessUpdate(input.NextSlice());
        break;
      case 3:
        fuzzer.GenerateUpdate();
        break;
    }
  }
}
