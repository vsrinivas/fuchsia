// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SERIAL_DRIVERS_FTDI_FTDI_MPSSE_H_
#define SRC_DEVICES_SERIAL_DRIVERS_FTDI_FTDI_MPSSE_H_

#include <fuchsia/hardware/serialimpl/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/sync/completion.h>
#include <lib/zx/event.h>
#include <lib/zx/time.h>
#include <stdint.h>
#include <threads.h>
#include <zircon/types.h>

#include <vector>

namespace ftdi_mpsse {

// This class represents FTDI's Multi-Process Synchronous Serial Engine.
// It is responsible for Reading and Writing to the underlying Serial driver.
// It is also responsible for doing setup work for things like GPIO pins
// and clock commands.
class Mpsse {
 public:
  enum Direction {
    IN,
    OUT,
  };
  enum Level {
    HIGH,
    LOW,
  };

  Mpsse(zx_device_t* parent) : ftdi_(parent) {}

  zx_status_t Init();
  zx_status_t IsValid() { return ftdi_.is_valid(); }
  zx_status_t Sync();
  zx_status_t Read(uint8_t* buf, size_t len);
  zx_status_t Write(uint8_t* buf, size_t len);

  zx_status_t SetGpio(int pin, Direction dir, Level lvl);
  void GpioWriteCommandToBuffer(size_t index, std::vector<uint8_t>* buffer, size_t* bytes_written);
  zx_status_t FlushGpio();

  zx_status_t SetClock(bool adaptive, bool three_phase, int hz);

 private:
  static void NotifyCallback(void* ctx, serial_state_t state);

  // Commands to set the GPIO pins levels and directions. Must be followed
  // by one byte of gpio levels and one byte of gpio directions. Lower pins
  // are pins 0-7 and higher pins are pins 8-15.
  static constexpr uint8_t kGpioSetCommandLowerPins = 0x80;
  static constexpr uint8_t kGpioSetCommandHigherPins = 0x82;

  static constexpr uint8_t kClockSetCommandByte1 = 0x8A;
  static constexpr uint8_t kClockSetCommandByte2 = 0x97;
  static constexpr uint8_t kClockSetCommandByte2AdaptiveOn = 0x96;
  static constexpr uint8_t kClockSetCommandByte3 = 0x8D;
  static constexpr uint8_t kClockSetCommandByte3ThreePhaseOn = 0x8C;
  static constexpr uint8_t kClockSetCommandByte4 = 0x86;

  static constexpr uint8_t kMpsseErrorInvalidCommand = 0xFA;

  static constexpr zx::duration kSerialReadWriteTimeout = zx::sec(1);
  static constexpr zx_signals_t kMpsseSignalReadable = ZX_USER_SIGNAL_0;
  static constexpr zx_signals_t kMpsseSignalWritable = ZX_USER_SIGNAL_1;

  ddk::SerialImplProtocolClient ftdi_ = {};
  uint16_t gpio_levels_ = 0;
  uint16_t gpio_directions_ = 0;

  sync_completion_t serial_readable_;
  sync_completion_t serial_writable_;
  serial_notify_t notify_cb_ = {};
};

}  // namespace ftdi_mpsse

#endif  // SRC_DEVICES_SERIAL_DRIVERS_FTDI_FTDI_MPSSE_H_
