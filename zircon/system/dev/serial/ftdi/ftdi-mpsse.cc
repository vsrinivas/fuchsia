// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftdi-mpsse.h"

namespace ftdi_mpsse {

zx_status_t Mpsse::Read(uint8_t* buf, size_t len) {
  size_t read_len = 0;
  zx_status_t status;
  uint8_t* buf_index = buf;

  while (read_len < len) {
    size_t actual;
    status = ftdi_.Read(buf_index, len - read_len, &actual);
    if (status != ZX_OK && status != ZX_ERR_SHOULD_WAIT) {
      return status;
    }
    read_len += actual;
    buf_index += actual;
  }
  return ZX_OK;
}

zx_status_t Mpsse::Write(uint8_t* buf, size_t len) {
  int retries = 0;
  uint8_t* buf_index = buf;
  size_t write_len = 0;

  while (write_len < len) {
    retries++;

    size_t actual;
    // TODO - As a performance improvement we should be able to wait on
    // WAITABLE somehow
    zx_status_t status = ftdi_.Write(buf_index, len - write_len, &actual);
    if (status != ZX_OK && status != ZX_ERR_SHOULD_WAIT) {
      return status;
    }
    write_len += actual;
    buf_index += actual;
  }

  return ZX_OK;
}

zx_status_t Mpsse::Sync() {
  constexpr uint8_t nonsense = 0xAB;
  uint8_t buf[2];
  // Send a nonsense command and then read the complaint.
  buf[0] = nonsense;
  zx_status_t status = Write(buf, 1);
  if (status != ZX_OK) {
    return status;
  }

  status = Read(buf, 2);
  if (status != ZX_OK) {
    return status;
  }

  // Check that the complaint matches.
  if (buf[0] != kMpsseErrorInvalidCommand || buf[1] != nonsense) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t Mpsse::SetGpio(int pin, Direction dir, Level lvl) {
  if (pin < 0 || pin > 15) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (dir == Direction::IN) {
    gpio_directions_ &= static_cast<uint16_t>(~(1U << pin) & 0xFFFF);
    gpio_levels_ &= static_cast<uint16_t>(~(1U << pin) & 0xFFFF);
  }
  if (dir == Direction::OUT) {
    gpio_directions_ |= static_cast<uint16_t>((1U << pin) & 0xFFFF);
    if (lvl == Level::LOW) {
      gpio_levels_ &= static_cast<uint16_t>(~(1U << pin) & 0xFFFF);
    } else {
      gpio_levels_ |= static_cast<uint16_t>((1U << pin) & 0xFFFF);
    }
  }
  return ZX_OK;
}

void Mpsse::GpioWriteCommandToBuffer(size_t index, std::vector<uint8_t>* buffer,
                                     size_t* bytes_written) {
  if (buffer->size() < index + 6) {
    buffer->resize(index + 6);
  }
  (*buffer)[index + 0] = kGpioSetCommandLowerPins;
  (*buffer)[index + 1] = static_cast<uint8_t>(gpio_levels_ & 0xFF);
  (*buffer)[index + 2] = static_cast<uint8_t>(gpio_directions_ & 0xFF);
  (*buffer)[index + 3] = kGpioSetCommandHigherPins;
  (*buffer)[index + 4] = static_cast<uint8_t>((gpio_levels_ >> 8) & 0xFF);
  (*buffer)[index + 5] = static_cast<uint8_t>((gpio_directions_ >> 8) & 0xFF);

  *bytes_written = 6;
}

zx_status_t Mpsse::FlushGpio() {
  std::vector<uint8_t> buf(6);
  size_t bytes_written;
  GpioWriteCommandToBuffer(0, &buf, &bytes_written);

  return Write(buf.data(), buf.size());
}

zx_status_t Mpsse::SetClock(bool adaptive, bool three_phase, int hz) {
  uint8_t buf[6];
  buf[0] = kClockSetCommandByte1;
  buf[1] = (adaptive) ? kClockSetCommandByte2AdaptiveOn : kClockSetCommandByte2;
  buf[2] = (three_phase) ? kClockSetCommandByte3ThreePhaseOn : kClockSetCommandByte3;

  int divisor = (30000000 - hz) / hz;
  if (three_phase) {
    divisor = (divisor * 2) / 3;
  }
  buf[3] = kClockSetCommandByte4;
  buf[4] = static_cast<uint8_t>(divisor & 0xFF);
  buf[5] = static_cast<uint8_t>((divisor >> 8) & 0xFF);

  return Write(buf, sizeof(buf));
}

}  // namespace ftdi_mpsse
