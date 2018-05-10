// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>
#include "linearizer.h"

namespace overnet {
namespace linearizer_fuzzer {

// Main fuzzer logic... to be used in the fuzzer itself, and in unit tests
class LinearizerFuzzer {
 public:
  static const uint64_t kBuffer = 1024;

  ~LinearizerFuzzer() { SetClosed(StatusCode::CANCELLED); }

  void Close(uint8_t status_code) {
    SetClosed(static_cast<StatusCode>(status_code));
    linearizer_.Close(static_cast<StatusCode>(status_code));
  }

  void Push(uint16_t offset, uint8_t length, const uint8_t* data) {
    if (uint64_t(offset) + length > offset_ + kBuffer) {
      SetClosed(StatusCode::RESOURCE_EXHAUSTED);
    }
    for (unsigned i = 0; i < length; i++) {
      unsigned idx = i + offset;
      ByteState& st = bytes_[idx];
      if (st.present) {
        if (st.byte != data[i] && idx >= offset_) {
          SetClosed(StatusCode::DATA_LOSS);
        }
      } else {
        st.byte = data[i];
        st.present = true;
      }
    }
    linearizer_.Push(Chunk{offset, Slice::FromCopiedBuffer(data, length)},
                     StatusCallback([this](const Status& status) {
                       if (is_closed_) {
                         assert(status.code() == closed_status_);
                       } else {
                         assert(status.is_ok());
                       }
                     }));
  }

  void Pull() {
    if (waiting_for_pull_) return;
    waiting_for_pull_ = true;
    linearizer_.Pull(StatusOrCallback<Optional<Slice>>(
        [this](const StatusOr<Optional<Slice>>& status) {
          assert(waiting_for_pull_);
          waiting_for_pull_ = false;
          #ifndef NDEBUG
          if (is_closed_) {
            assert(status.code() == closed_status_);
          } else {
            assert(status.is_ok());
            for (auto b : **status.get()) {
              ByteState st = bytes_[offset_++];
              assert(st.present);
              assert(b == st.byte);
            }
          }
          #endif
        }));
  }

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
  uint64_t offset_ = 0;
  ByteState bytes_[(1 << 16) + (1 << 8)];
  bool is_closed_ = false;
  StatusCode closed_status_;
  bool waiting_for_pull_ = false;

  Linearizer linearizer_{kBuffer};
};

}  // namespace linearizer_fuzzer
}  // namespace overnet
