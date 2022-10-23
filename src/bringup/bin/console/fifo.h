// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_CONSOLE_FIFO_H_
#define SRC_BRINGUP_BIN_CONSOLE_FIFO_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/eventpair.h>
#include <stdint.h>
#include <stdlib.h>
#include <zircon/types.h>

#include <mutex>
#include <utility>

class Fifo {
 public:
  // The eventpair given to this will be used to signal when the Fifo is
  // readable (via the DEVICE_SIGNAL_READABLE signal).  WRITABLE signals are not
  // issued, since the consumer of this class uses it to implement the RX queue
  // on the console and not the TX queue.
  explicit Fifo(zx::eventpair event) : event_{std::move(event)} {}

  // Fill the given buffer with data from this FIFO.  Since short reads may
  // occur, a new string piece is returned which refers to the same buffer as
  // |in|, but may be shorter.
  zx_status_t Read(uint8_t* buffer, size_t len, size_t* actual);
  zx_status_t Write(const uint8_t* buffer, size_t len, size_t* actual);

  // The public facing size is one byte smaller than the actual size, due to the
  // use of a sentinel byte for detecting the full case.
  static inline constexpr uint32_t kFifoSize = 255;

 private:
  zx_status_t ReadByteLocked(uint8_t* out) TA_REQ(lock_);
  zx_status_t WriteByteLocked(uint8_t x) TA_REQ(lock_);

  bool IsEmptyLocked() TA_REQ(lock_) { return head_ == tail_; }
  bool IsFullLocked() TA_REQ(lock_) { return ((head_ + 1) & kFifoMask) == tail_; }

  static inline constexpr uint32_t kFifoMask = kFifoSize;

  std::mutex lock_;

  uint8_t data_[kFifoSize + 1] TA_GUARDED(lock_) = {};
  uint32_t head_ TA_GUARDED(lock_) = 0;
  uint32_t tail_ TA_GUARDED(lock_) = 0;

  zx::eventpair event_;
};

#endif  // SRC_BRINGUP_BIN_CONSOLE_FIFO_H_
