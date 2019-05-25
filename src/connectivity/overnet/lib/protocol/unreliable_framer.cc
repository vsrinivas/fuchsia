// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/protocol/unreliable_framer.h"

#include "third_party/zlib/zlib.h"

namespace overnet {

UnreliableFramer::UnreliableFramer() : StreamFramer(Border{2, 4}, 256) {}

UnreliableFramer::~UnreliableFramer() = default;

void UnreliableFramer::Push(Slice data) {
  buffered_input_.Append(std::move(data));
}

StatusOr<Optional<Slice>> UnreliableFramer::Pop() {
  using Sts = StatusOr<Optional<Slice>>;

  const uint8_t *const begin = buffered_input_.begin();
  const uint8_t *p = begin;
  const uint8_t *const end = buffered_input_.end();

  while (p != end) {
    if (*p != kStartOfFrameMarker) {
      ++p;
      continue;
    }

    if (end - p < 7) {
      break;
    }

    uint32_t length = uint32_t(p[1]) + 1;
    if (end - p < 2 + length + 4) {
      break;
    }

    uint32_t sent_crc;
    memcpy(&sent_crc, p + 2 + length, sizeof(sent_crc));
    const uint32_t calc_crc = crc32(0, p + 2, length);

    if (sent_crc != calc_crc) {
      ++p;
      continue;
    }

    auto out = buffered_input_;
    out.Trim(p + 2 - begin, end - (p + 2) - length);
    buffered_input_.TrimBegin(p + 2 + length + 4 - begin);
    return Sts(out);
  }

  buffered_input_.TrimBegin(p - begin);
  return Sts(Nothing);
}

bool UnreliableFramer::InputEmpty() const {
  return buffered_input_.length() == 0;
}

Optional<Slice> UnreliableFramer::SkipNoise() {
  const uint8_t *const begin = buffered_input_.begin();
  const uint8_t *p = begin;
  const uint8_t *const end = buffered_input_.end();

  if (p == end) {
    return Nothing;
  }
  if (*p != kStartOfFrameMarker) {
    return Nothing;
  }
  ++p;
  while (p != end) {
    if (*p != kStartOfFrameMarker) {
      ++p;
      continue;
    }

    if (end - p < 7) {
      break;
    }

    uint32_t length = uint32_t(p[1]) + 1;
    if (end - p < 2 + length + 4) {
      break;
    }

    uint32_t sent_crc;
    memcpy(&sent_crc, p + 2 + length, sizeof(sent_crc));
    const uint32_t calc_crc = crc32(0, p + 2, length);

    if (sent_crc != calc_crc) {
      ++p;
      continue;
    }

    break;
  }

  return buffered_input_.TakeUntilPointer(p);
}

Slice UnreliableFramer::Frame(Slice data) {
  const auto length = data.length();
  if (length == 0) {
    return data;
  }
  assert(length <= 256);
  return data.WithBorders(Border{2, 4}, [length](uint8_t *p) {
    p[0] = kStartOfFrameMarker;
    p[1] = length - 1;
    auto *s = p + 2 + length;
    const uint32_t crc = crc32(0, p + 2, length);
    memcpy(s, &crc, sizeof(crc));
  });
}

}  // namespace overnet
