// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_PARSER_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_PARSER_H_

#include <lib/zx/bti.h>
#include <lib/zx/event.h>
#include <lib/zx/handle.h>
#include <zircon/types.h>

#include <memory>
#include <mutex>
#include <thread>

#include <ddk/io-buffer.h>

#include "macros.h"

class DecoderCore;
class DecoderInstance;
struct MmioRegisters;
class StreamBuffer;

class Parser final {
 public:
  class Owner {
   public:
    virtual __WARN_UNUSED_RESULT zx::unowned_bti bti() = 0;
    virtual __WARN_UNUSED_RESULT MmioRegisters* mmio() = 0;
    [[nodiscard]] virtual bool is_parser_gated() const = 0;
  };

  Parser(Owner* owner, zx::handle interrupt_handle);
  ~Parser();

  __WARN_UNUSED_RESULT
  zx_status_t InitializeEsParser(DecoderInstance* instance);
  __WARN_UNUSED_RESULT
  zx_status_t ParseVideo(const void* data, uint32_t len);
  __WARN_UNUSED_RESULT
  zx_status_t ParseVideoPhysical(zx_paddr_t paddr, uint32_t len);

  // If parser_running_, try to cause WaitForParsingCompleted() to return
  // ZX_ERR_CANCELED ASAP.  If !parser_running_, do nothing.  The caller is
  // responsible for ensuring that only its own decoder's work is ever canceled.
  void TryStartCancelParsing();
  // Any error: The caller should call CancelParsing() to clean up.
  // ZX_ERR_CANCELED: TryStartCancelParsing() was called and the caller should
  //   call CancelParsing() to cancel the parsing, just as the caller
  //   does for any error from WaitForParsingCompleted().  This error code in
  //   this context can be thought of as ZX_ERR_YOU_SHOULD_CANCEL_PARSING_NOW.
  //   It's not an indication that parsing is already canceled, only that the
  //   caller should call CancelParsing().
  // ZX_OK: The parsing is done.  If the caller called TryStartCancelParsing()
  //   at some point, no harm done.  The caller should not call CancelParsing().
  __WARN_UNUSED_RESULT
  zx_status_t WaitForParsingCompleted(zx_duration_t deadline);
  void CancelParsing();
  void SetOutputLocation(zx_paddr_t paddr, uint32_t len);

  // Set the parser output buffer and ringbuffer pointers from a current decoder instance.
  void SyncFromDecoderInstance(DecoderInstance*);
  // Copy the parser write pointer into a current decoder instance. Only the write pointer is synced
  // because it's assumed that the decoder has up-to-date copies of the other input registers. In
  // particular, it might have processed video and modified the read pointer since the last
  // SyncFromDecoderInstance.
  void SyncToDecoderInstance(DecoderInstance*);

 private:
  void SyncFromBufferParameters(uint32_t buffer_phys_address, uint32_t buffer_size,
                                uint32_t read_offset, uint32_t write_offset);

  Owner* owner_;
  zx::handle interrupt_handle_;
  std::unique_ptr<io_buffer_t> parser_input_;

  // This buffer holds an ES start code that's used to get an interrupt when the
  // parser is finished.
  io_buffer_t search_pattern_ = {};
  // ZX_USER_SIGNAL_0 is for parser done.
  // ZX_USER_SIGNAL_1 is for client wants ParseVideo() to return
  //   ZX_ERR_CANCELED ASAP.
  //
  // Both must be un-signaled while parser_running_ is false (transients while
  // under parser_running_lock_ are fine).
  //
  // While parser_running_ is true, either can become signaled as appropriate.
  zx::event parser_finished_event_;

  std::mutex parser_running_lock_;
  bool parser_running_ = false;
  std::thread parser_interrupt_thread_;
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_PARSER_H_
