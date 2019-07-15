// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "frame.h"

#include <endian.h>

#include <algorithm>

#include "fbl/span.h"
#include "fcs.h"

namespace ppp {

static constexpr uint8_t kAllStationsAddress = 0xff;
static constexpr uint8_t kControlField = 0x03;

static constexpr size_t kMaxFrameSize = 65536;

static constexpr uint8_t kControlEscapeSequence = 0x7d;
static constexpr uint8_t kControlEscapeComplement = 0x20;

static constexpr uint8_t kAsciiControl = 0x20;

static constexpr uint16_t kFrameCheckSequenceComplement = 0xffff;

static void PushEscaped(std::vector<uint8_t>* buffer, uint8_t b) {
  if (b < kAsciiControl || b == kControlEscapeSequence || b == kFlagSequence) {
    buffer->push_back(kControlEscapeSequence);
    buffer->push_back(b ^ kControlEscapeComplement);
  } else {
    buffer->push_back(b);
  }
}

static uint16_t FrameCheck(FrameView frame) {
  uint16_t fcs = kFrameCheckSequenceInit;
  fcs = Fcs(fcs, fbl::Span<const uint8_t>(&kAllStationsAddress, 1));
  fcs = Fcs(fcs, fbl::Span<const uint8_t>(&kControlField, 1));

  const uint8_t protocol_upper = static_cast<uint16_t>(frame.protocol) >> 8;
  const uint8_t protocol_lower = static_cast<uint16_t>(frame.protocol) & 0xff;
  fcs = Fcs(fcs, fbl::Span<const uint8_t>(&protocol_upper, 1));
  fcs = Fcs(fcs, fbl::Span<const uint8_t>(&protocol_lower, 1));

  fcs = Fcs(fcs, frame.information);
  return fcs;
}

std::vector<uint8_t> SerializeFrame(FrameView frame) {
  std::vector<uint8_t> buffer;
  buffer.reserve(kMaxFrameSize);

  buffer.push_back(kFlagSequence);

  PushEscaped(&buffer, kAllStationsAddress);
  PushEscaped(&buffer, kControlField);

  const uint8_t protocol_upper = static_cast<uint16_t>(frame.protocol) >> 8;
  const uint8_t protocol_lower = static_cast<uint16_t>(frame.protocol) & 0xff;
  PushEscaped(&buffer, protocol_upper);
  PushEscaped(&buffer, protocol_lower);

  for (uint8_t b : frame.information) {
    PushEscaped(&buffer, b);
  }

  const uint16_t fcs = FrameCheck(frame);
  const uint16_t complement = fcs ^ kFrameCheckSequenceComplement;
  const uint8_t complement_upper = complement >> 8;
  const uint8_t complement_lower = complement & 0xff;

  // FCS bytes are written in reverse of network order
  PushEscaped(&buffer, complement_lower);
  PushEscaped(&buffer, complement_upper);

  buffer.push_back(kFlagSequence);

  return buffer;
}

fit::result<Frame, DeserializationError> DeserializeFrame(fbl::Span<const uint8_t> raw_frame) {
  std::vector<uint8_t> buffer;
  buffer.reserve(raw_frame.size());

  auto it = raw_frame.begin();

  while (it != raw_frame.end()) {
    const auto b = *it;
    switch (b) {
      case kFlagSequence:
        if (it != raw_frame.begin() && it != raw_frame.end() - 1) {
          return fit::error(DeserializationError::FormatInvalid);
        }
        buffer.push_back(b);
        break;
      case kControlEscapeSequence:
        ++it;
        if (it == raw_frame.end()) {
          return fit::error(DeserializationError::FormatInvalid);
        }
        buffer.push_back(*it ^ kControlEscapeComplement);
        break;
      default:
        if (b >= kAsciiControl) {
          buffer.push_back(b);
        }
        break;
    }
    ++it;
  }

  if (buffer.size() < 8) {
    return fit::error(DeserializationError::FormatInvalid);
  }

  if (buffer.front() != kFlagSequence || buffer.back() != kFlagSequence) {
    return fit::error(DeserializationError::FormatInvalid);
  }

  const uint8_t* begin_frame = buffer.data() + 1;
  const size_t frame_size = buffer.size() - 2;

  const uint8_t address = begin_frame[0];

  if (address != kAllStationsAddress) {
    return fit::error(DeserializationError::UnrecognizedAddress);
  }

  const uint8_t control = begin_frame[1];

  if (control != kControlField) {
    return fit::error(DeserializationError::UnrecognizedControl);
  }

  const uint8_t protocol_upper = begin_frame[2];
  const uint8_t protocol_lower = begin_frame[3];
  const auto protocol = static_cast<Protocol>((protocol_upper << 8) | protocol_lower);

  const uint16_t fcs =
      Fcs(kFrameCheckSequenceInit, fbl::Span<const uint8_t>(begin_frame, frame_size));
  if (fcs != kFrameCheckSequence) {
    return fit::error(DeserializationError::FailedFrameCheckSequence);
  }

  // Erase everything but the information from the buffer.
  const auto information_end = std::rotate(buffer.begin(), buffer.begin() + 5, buffer.end() - 3);
  buffer.erase(information_end, buffer.end());

  return fit::ok(Frame(protocol, std::move(buffer)));
}

}  // namespace ppp
