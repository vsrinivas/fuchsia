// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/protocol/reliable_framer.h"

#include "src/connectivity/overnet/lib/environment/trace.h"

namespace overnet {

ReliableFramer::ReliableFramer() : StreamFramer(Border::Prefix(2), 65536) {}
ReliableFramer::~ReliableFramer() = default;

void ReliableFramer::Push(Slice data) {
  OVERNET_TRACE(DEBUG) << "ReliableFramer.Push: " << data;
  buffered_input_.Append(std::move(data));
}

StatusOr<Optional<Slice>> ReliableFramer::Pop() {
  OVERNET_TRACE(DEBUG) << "ReliableFramer.Pop: q=" << buffered_input_;
  using Sts = StatusOr<Optional<Slice>>;

  const uint8_t *begin = buffered_input_.begin();
  const uint8_t *p = begin;
  const uint8_t *end = buffered_input_.end();

  OVERNET_TRACE(DEBUG) << "ReliableFramer.Pop: have " << (end - p) << " bytes";

  if (end - p < 2) {
    OVERNET_TRACE(DEBUG)
        << "ReliableFramer.Pop: insufficient bytes to see header";
    return Nothing;
  }
  uint16_t hdr;
  memcpy(&hdr, p, 2);
  p += 2;

  const ssize_t segment_length = ssize_t(hdr) + 1;
  OVERNET_TRACE(DEBUG) << "ReliableFramer.Pop: hdr=" << hdr
                       << " => segment_length " << segment_length;

  if (end - p < segment_length) {
    return Sts(Nothing);
  }

  buffered_input_.TrimBegin(p - begin);
  return Sts(buffered_input_.TakeUntilOffset(segment_length));
}

bool ReliableFramer::InputEmpty() const {
  return buffered_input_.length() == 0;
}

Optional<Slice> ReliableFramer::SkipNoise() { return Nothing; }

Slice ReliableFramer::Frame(Slice data) {
  OVERNET_TRACE(DEBUG) << "ReliableFramer.Frame: " << data;
  auto length = data.length();

  if (length == 0) {
    return data;
  }

  return data.WithPrefix(2, [length](uint8_t *p) {
    uint16_t hdr = length - 1;
    memcpy(p, &hdr, 2);
  });
}

}  // namespace overnet
