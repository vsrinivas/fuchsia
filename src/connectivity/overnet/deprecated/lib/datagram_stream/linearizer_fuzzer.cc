// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/deprecated/lib/datagram_stream/linearizer_fuzzer.h"

namespace overnet {
namespace linearizer_fuzzer {

// Close the linearizer with some status code.
void LinearizerFuzzer::Close(uint8_t status_code) {
  if (status_code == 0) {
    if (!length_.has_value()) {
      status_code = 1;
    } else if (offset_ != *length_) {
      status_code = 1;
    } else {
      for (uint64_t i = 0; i < *length_; i++) {
        if (!bytes_[i].present) {
          status_code = 1;
        }
      }
    }
  }
  SetClosed(static_cast<StatusCode>(status_code));
  linearizer_.Close(static_cast<StatusCode>(status_code)).Ignore();
}

// Push a new block onto the linearizer at offset 'offset', with length
// 'length', an end_of_message flag, and data bytes in 'data'.
void LinearizerFuzzer::Push(uint16_t offset, uint8_t length, bool end_of_message,
                            const uint8_t* data) {
  uint64_t last_byte = static_cast<uint64_t>(offset) + length;
  const bool resource_exhausted = last_byte > offset_ + kBuffer;
  if (!resource_exhausted) {
    if (length_) {
      if (last_byte > *length_) {
        SetClosed(StatusCode::INVALID_ARGUMENT);
      } else if (end_of_message && *length_ != last_byte) {
        SetClosed(StatusCode::INVALID_ARGUMENT);
      }
    } else if (end_of_message) {
      if (offset_ > last_byte)
        SetClosed(StatusCode::INVALID_ARGUMENT);
      for (unsigned i = last_byte; i < sizeof(bytes_) / sizeof(*bytes_); i++) {
        if (bytes_[i].present)
          SetClosed(StatusCode::INVALID_ARGUMENT);
      }
      if (offset_ == last_byte)
        SetClosed(StatusCode::OK);
      length_ = last_byte;
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
  }
  auto ignore = [](bool) {};
  ignore(linearizer_.Push(Chunk{offset, end_of_message, Slice::FromCopiedBuffer(data, length)}));
}

// Execute a pull op on the linearizer, and verify that it's as expected.
void LinearizerFuzzer::Pull() {
  if (waiting_for_pull_)
    return;
  waiting_for_pull_ = true;
  linearizer_.Pull(
      StatusOrCallback<Optional<Slice>>([this](const StatusOr<Optional<Slice>>& status) {
        assert(waiting_for_pull_);
        waiting_for_pull_ = false;
#ifndef NDEBUG
        if (is_closed_) {
          assert(status.code() == closed_status_);
        } else {
          assert(status.is_ok());
          if (*status) {
            for (auto b : **status) {
              ByteState st = bytes_[offset_++];
              assert(st.present);
              assert(b == st.byte);
            }
          }
          if (length_ && *length_ == offset_) {
            SetClosed(StatusCode::OK);
          }
        }
#endif
      }));
}

}  // namespace linearizer_fuzzer
}  // namespace overnet
