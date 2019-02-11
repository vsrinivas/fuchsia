// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_L2CAP_INTERNAL_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_L2CAP_INTERNAL_H_

#include <endian.h>

#include <cstdint>
#include <type_traits>

#include <zircon/assert.h>
#include <zircon/compiler.h>

namespace btlib {
namespace l2cap {
namespace internal {

// The definitions within this namespace don't directly map to full frame
// formats. Rather, they provide access to mode-specific headers beyond the
// L2CAP basic frame header.

// For Retransmission and Flow Control Modes. (Vol 3, Part A, Sec 3.3.2)
using StandardControlField = uint16_t;

// For Enhanced Retransmission and Streaming Modes _without_ Extended Window
// Size. (Vol 3, Part A, Sec 3.3.2)
struct EnhancedControlField {
  bool designates_supervisory_frame() const { return le16toh(raw_value) & 0x1; }
  bool designates_start_of_segmented_sdu() const {
    return !designates_supervisory_frame() &&
           ((le16toh(raw_value) & (0b11 << 14)) == (0b01 << 14));
  }
  // Returns true for all segmented frames, including the start-of-segment frame
  // (even though the start-of-segment frame has a different header format).
  bool designates_part_of_segmented_sdu() const {
    return !designates_supervisory_frame() &&
           (le16toh(raw_value) & (0b11 << 14));
  }

 protected:
  uint16_t raw_value;  // In protocol byte-order (little-endian).
} __PACKED;

// For Enhanced Retransmission and Streaming Modes _with_ Extended Window
// Size. (Vol 3, Part A, Secs 3.3.2 and 5.7. Feature 2/39.)
using ExtendedControlField = uint32_t;

// Represents an I-frame header for:
// * a channel operating in Enhanced Retransmission or
//   Streaming Mode, where
// * the Extended Window Size and Frame Checksum options are
//   disabled, and
// * the frame is _not_ a "Start of L2CAP SDU" frame.
// Omits the Basic L2CAP header. See Vol 3, Part A, Sec 3.3.
struct SimpleInformationFrameHeader : public EnhancedControlField {
  uint8_t tx_seq() const {
    ZX_DEBUG_ASSERT(!designates_supervisory_frame());
    return (le16toh(raw_value) & (0b01111110)) >> 1;
  }
} __PACKED;
static_assert(std::is_standard_layout_v<SimpleInformationFrameHeader>);

// Represents an I-frame header for:
// * a channel operating in Enhanced Retransmission or
//   Streaming Mode, where
// * the Extended Window Size and Frame Checksum options are
//   disabled, and
// * the frame _is_ a "Start of L2CAP SDU" frame.
// Omits the Basic L2CAP header. See Vol 3, Part A, Sec 3.3.
struct SimpleStartOfSduFrameHeader : public SimpleInformationFrameHeader {
  uint16_t sdu_len;
} __PACKED;
static_assert(std::is_standard_layout_v<SimpleInformationFrameHeader>);

// Represents an S-frame for:
// * a channel operating in Enhanced Retransmission or
//   Streaming Mode, where
// * the Extended Window Size and Frame Checksum options are
//   disabled
// Omits the Basic L2CAP header. See Vol 3, Part A, Sec 3.3.
struct SimpleSupervisoryFrame : public EnhancedControlField {
} __PACKED;
static_assert(std::is_standard_layout_v<SimpleInformationFrameHeader>);

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_L2CAP_INTERNAL_H_
