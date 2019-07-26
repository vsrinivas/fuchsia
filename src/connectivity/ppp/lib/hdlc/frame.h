// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/span.h>
#include <lib/fit/result.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "lib/common/ppp.h"

namespace ppp {

static constexpr uint8_t kFlagSequence = 0x7e;

struct FrameView {
  FrameView(Protocol protocol, fbl::Span<const uint8_t> information)
      : protocol(protocol), information(information) {}
  Protocol protocol;
  fbl::Span<const uint8_t> information;
};

struct Frame {
  Frame(Protocol protocol, std::vector<uint8_t> information)
      : protocol(protocol), information(std::move(information)) {}
  Protocol protocol;
  std::vector<uint8_t> information;
};

enum class DeserializationError {
  UnrecognizedAddress,
  UnrecognizedControl,
  FailedFrameCheckSequence,
  FormatInvalid,
};

std::vector<uint8_t> SerializeFrame(FrameView frame);

fit::result<Frame, DeserializationError> DeserializeFrame(fbl::Span<const uint8_t> raw_frame);

}  // namespace ppp
