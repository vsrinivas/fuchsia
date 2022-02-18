// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_USB_PACKET_REASSEMBLER_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_USB_PACKET_REASSEMBLER_H_

#include <lib/ddk/debug.h>
#include <lib/fit/function.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace bt_transport_usb {

// PacketReassembler is a utility for accumulating packet chunks and reporting when a complete
// packet has been accumulated. It is intended to be used for SCO packets and HCI event packets,
// which both have a 1-byte length field in the header. |kMaxPacketSize| is the max frame size of
// the packet this reasembler is being used for.
template <size_t kMaxPacketSize>
class PacketReassembler {
 public:
  using PacketCallback = fit::function<void(cpp20::span<const uint8_t>)>;
  // |length_param_index| is the index in the header of the 1-byte header field.
  // |header_size| is the size of the packet header.
  // |packet_cb| is called with packets as soon as a full packet is
  // accumulated via ProcessData().
  PacketReassembler(size_t length_param_index, size_t header_size, PacketCallback packet_cb)
      : length_param_index_(length_param_index),
        header_size_(header_size),
        packet_cb_(std::move(packet_cb)) {
    ZX_ASSERT(kMaxPacketSize <= header_size + std::numeric_limits<uint8_t>::max());
    ZX_ASSERT(length_param_index_ < header_size_);
  }

  void clear() {
    accumulated_bytes_ = 0;
    accumulated_.fill(0);
  }

  // Append data to the accumulator and call packet handler if a complete packet has been
  // accumulated. |buffer| may contain data from multiple packets.
  void ProcessData(cpp20::span<const uint8_t> buffer) {
    // If nothing has accumulated, report complete packets in buffer and accumulate the rest.
    if (accumulated_bytes_ == 0) {
      ReportCompletePacketsAndAccumulateRemainder(buffer);
      return;
    }

    // Otherwise, try to complete accumulated packet.
    uint8_t data_length = 0;
    // The length parameter may be in |buffer|.
    if (accumulated_bytes_ <= length_param_index_) {
      // The length parameter may not be in |accumulated_| or |buffer|. In this case, just
      // accumulate |buffer|.
      if (accumulated_bytes_ + buffer.size() <= length_param_index_) {
        Accumulate(buffer);
        return;
      }
      data_length = buffer[length_param_index_ - accumulated_bytes_];
    } else {
      data_length = accumulated_[length_param_index_];
    }

    const size_t packet_size = header_size_ + data_length;
    const size_t remaining_needed = packet_size - accumulated_bytes_;

    // If the buffer completes a packet in the accumulator, report that packet before processing
    // additional packets.
    if (buffer.size() >= remaining_needed) {
      Accumulate(buffer.subspan(0, remaining_needed));
      size_t buffer_offset = remaining_needed;
      packet_cb_({accumulated_.data(), packet_size});
      clear();

      ReportCompletePacketsAndAccumulateRemainder(buffer.subspan(buffer_offset));
      return;
    }

    // |buffer| does not complete the packet. Add to accumulator.
    Accumulate(buffer);
  }

 private:
  void ReportCompletePacketsAndAccumulateRemainder(cpp20::span<const uint8_t> buffer) {
    size_t offset = ReportCompletePackets(buffer);
    Accumulate(buffer.subspan(offset));
  }

  // Returns the index of the byte after the last complete packet (or 0 if no complete packets
  // exist).
  size_t ReportCompletePackets(cpp20::span<const uint8_t> buffer) {
    size_t buffer_offset = 0;
    // Report all complete packets in buffer (avoiding copies).
    while (buffer_offset + header_size_ < buffer.size()) {
      const uint8_t data_length = buffer[buffer_offset + length_param_index_];
      const size_t packet_size = header_size_ + data_length;
      if (buffer.size() < buffer_offset + packet_size) {
        break;
      }
      packet_cb_(buffer.subspan(buffer_offset, packet_size));
      buffer_offset += packet_size;
    }
    return buffer_offset;
  }

  void Accumulate(cpp20::span<const uint8_t> buffer) {
    std::copy(buffer.begin(), buffer.end(), accumulated_.begin() + accumulated_bytes_);
    accumulated_bytes_ += buffer.size();
  }

  const size_t length_param_index_;
  const size_t header_size_;
  std::array<uint8_t, kMaxPacketSize> accumulated_;
  // The size of the valid accumulated data in accumulated_.
  size_t accumulated_bytes_ = 0;
  PacketCallback packet_cb_;
};

}  // namespace bt_transport_usb

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_USB_PACKET_REASSEMBLER_H_
