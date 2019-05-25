// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "src/connectivity/overnet/lib/datagram_stream/linearizer.h"
#include "src/connectivity/overnet/lib/environment/trace_cout.h"
#include "src/connectivity/overnet/lib/testing/test_timer.h"
#include "src/connectivity/overnet/lib/vocabulary/optional.h"

namespace overnet {
namespace linearizer_fuzzer {

// Main fuzzer logic... to be used in the fuzzer itself, and in unit tests.
// Includes a simple but inefficient emulation of what the real linearizer does
// efficiently, and compares that the obtained results are identical.
class LinearizerFuzzer {
 public:
  static constexpr inline uint64_t kBuffer = 1024;

  LinearizerFuzzer(bool log_stuff)
      : logging_(log_stuff ? new Logging(&timer_) : nullptr) {}

  ~LinearizerFuzzer() { SetClosed(StatusCode::CANCELLED); }

  // Close the linearizer with some status code.
  void Close(uint8_t status_code);

  // Push a new block onto the linearizer at offset 'offset', with length
  // 'length', an end_of_message flag, and data bytes in 'data'.
  void Push(uint16_t offset, uint8_t length, bool end_of_message,
            const uint8_t* data);

  // Execute a pull op on the linearizer, and verify that it's as expected.
  void Pull();

 private:
  void SetClosed(StatusCode status_code) {
    if (!is_closed_) {
      is_closed_ = true;
      closed_status_ = status_code;
    }
  }

  struct ByteState {
    bool present = false;
    uint8_t byte = 0;
  };

  TestTimer timer_;
  struct Logging {
    Logging(Timer* timer) : tracer(timer) {}
    TraceCout tracer;
    ScopedRenderer set_tracer{&tracer};
  };
  std::unique_ptr<Logging> logging_;

  uint64_t offset_ = 0;
  Optional<uint64_t> length_;
  ByteState bytes_[(1 << 16) + (1 << 8)];
  bool is_closed_ = false;
  StatusCode closed_status_;
  bool waiting_for_pull_ = false;

  StreamStats stats_;
  Linearizer linearizer_{kBuffer, &stats_};
};

}  // namespace linearizer_fuzzer
}  // namespace overnet
