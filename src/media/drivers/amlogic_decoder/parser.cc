// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "parser.h"

#include <lib/trace/event.h>

#include <limits>

#include "decoder_core.h"
#include "decoder_instance.h"
#include "src/media/lib/memory_barriers/memory_barriers.h"
#include "stream_buffer.h"
#include "util.h"

Parser::Parser(Owner* owner, zx::handle interrupt_handle)
    : owner_(owner), interrupt_handle_(std::move(interrupt_handle)) {
  zx::event::create(0, &parser_finished_event_);
}

Parser::~Parser() {
  if (interrupt_handle_) {
    zx_interrupt_destroy(interrupt_handle_.get());
    if (parser_interrupt_thread_.joinable())
      parser_interrupt_thread_.join();
  }
  CancelParsing();
  if (parser_input_) {
    io_buffer_release(parser_input_.get());
    parser_input_.reset();
  }
  io_buffer_release(&search_pattern_);
}

// This parser handles MPEG elementary streams.
zx_status_t Parser::InitializeEsParser(DecoderInstance* instance) {
  assert(!owner_->is_parser_gated());
  Reset1Register::Get().FromValue(0).set_parser(true).WriteTo(owner_->mmio()->reset);
  FecInputControl::Get().FromValue(0).WriteTo(owner_->mmio()->demux);
  TsHiuCtl::Get()
      .ReadFrom(owner_->mmio()->demux)
      .set_use_hi_bsf_interface(false)
      .WriteTo(owner_->mmio()->demux);
  TsHiuCtl2::Get()
      .ReadFrom(owner_->mmio()->demux)
      .set_use_hi_bsf_interface(false)
      .WriteTo(owner_->mmio()->demux);
  TsHiuCtl3::Get()
      .ReadFrom(owner_->mmio()->demux)
      .set_use_hi_bsf_interface(false)
      .WriteTo(owner_->mmio()->demux);
  TsFileConfig::Get()
      .ReadFrom(owner_->mmio()->demux)
      .set_ts_hiu_enable(false)
      .WriteTo(owner_->mmio()->demux);
  ParserConfig::Get()
      .FromValue(0)
      .set_pfifo_empty_cnt(10)
      .set_max_es_write_cycle(1)
      .set_max_fetch_cycle(16)
      .WriteTo(owner_->mmio()->parser);
  PfifoRdPtr::Get().FromValue(0).WriteTo(owner_->mmio()->parser);
  PfifoWrPtr::Get().FromValue(0).WriteTo(owner_->mmio()->parser);
  constexpr uint32_t kEsStartCodePattern = 0x00000100;
  constexpr uint32_t kEsStartCodeMask = 0xffffff00;
  ParserSearchPattern::Get().FromValue(kEsStartCodePattern).WriteTo(owner_->mmio()->parser);
  ParserSearchMask::Get().FromValue(kEsStartCodeMask).WriteTo(owner_->mmio()->parser);

  ParserConfig::Get()
      .FromValue(0)
      .set_pfifo_empty_cnt(10)
      .set_max_es_write_cycle(1)
      .set_max_fetch_cycle(16)
      .set_startcode_width(ParserConfig::kWidth24)
      .set_pfifo_access_width(ParserConfig::kWidth8)
      .WriteTo(owner_->mmio()->parser);

  ParserControl::Get().FromValue(ParserControl::kAutoSearch).WriteTo(owner_->mmio()->parser);

  if (instance) {
    // Set up output fifo.
    uint32_t buffer_address = truncate_to_32(instance->stream_buffer()->buffer().phys_base());
    ParserVideoStartPtr::Get().FromValue(buffer_address).WriteTo(owner_->mmio()->parser);
    ParserVideoEndPtr::Get()
        .FromValue(buffer_address + instance->stream_buffer()->buffer().size() - 8)
        .WriteTo(owner_->mmio()->parser);

    ParserEsControl::Get()
        .ReadFrom(owner_->mmio()->parser)
        .set_video_manual_read_ptr_update(false)
        .set_video_write_endianness(0x7)
        .WriteTo(owner_->mmio()->parser);

    instance->core()->InitializeParserInput();
  }

  if (!io_buffer_is_valid(&search_pattern_)) {
    // 512 bytes includes some padding to force the parser to read it completely.
    constexpr uint32_t kSearchPatternSize = 512;
    zx_status_t status = io_buffer_init(&search_pattern_, owner_->bti()->get(), kSearchPatternSize,
                                        IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
      DECODE_ERROR("Failed to create search pattern buffer");
      return status;
    }
    SetIoBufferName(&search_pattern_, "ParserSearchPattern");

    uint8_t input_search_pattern[kSearchPatternSize] = {0, 0, 1, 0xff};

    memcpy(io_buffer_virt(&search_pattern_), input_search_pattern, kSearchPatternSize);
    io_buffer_cache_flush(&search_pattern_, 0, kSearchPatternSize);
    BarrierAfterFlush();
  }

  // This check exists so we can call InitializeEsParser() more than once, when
  // called from CodecImpl (indirectly via a CodecAdapter).
  if (!parser_interrupt_thread_.joinable()) {
    parser_interrupt_thread_ = std::thread([this]() {
      DLOG("Starting parser thread");
      while (true) {
        zx_time_t time;
        zx_status_t zx_status = zx_interrupt_wait(interrupt_handle_.get(), &time);
        if (zx_status != ZX_OK)
          return;

        std::lock_guard<std::mutex> lock(parser_running_lock_);
        if (!parser_running_)
          continue;
        assert(!owner_->is_parser_gated());
        // Continue holding parser_running_lock_ to ensure a cancel doesn't
        // execute while signaling is happening.
        auto status = ParserIntStatus::Get().ReadFrom(owner_->mmio()->parser);
        // Clear interrupt.
        status.WriteTo(owner_->mmio()->parser);
        DLOG("Got Parser interrupt status %x", status.reg_value());
        if (status.start_code_found()) {
          PfifoRdPtr::Get().FromValue(0).WriteTo(owner_->mmio()->parser);
          PfifoWrPtr::Get().FromValue(0).WriteTo(owner_->mmio()->parser);
          parser_finished_event_.signal(0, ZX_USER_SIGNAL_0);
        }
      }
    });
  }

  ParserIntStatus::Get().FromValue(0xffff).WriteTo(owner_->mmio()->parser);
  ParserIntEnable::Get().FromValue(0).set_host_en_start_code_found(true).WriteTo(
      owner_->mmio()->parser);

  return ZX_OK;
}

void Parser::SetOutputLocation(zx_paddr_t paddr, uint32_t len) {
  uint32_t buffer_start = truncate_to_32(paddr);
  ParserVideoStartPtr::Get().FromValue(buffer_start).WriteTo(owner_->mmio()->parser);
  // Prevent the parser from writing off the end of the buffer. Seems like it
  // probably needs to be 8-byte aligned.
  constexpr uint32_t kEndOfBufferOffset = 8;
  ParserVideoEndPtr::Get()
      .FromValue(buffer_start + len - kEndOfBufferOffset)
      .WriteTo(owner_->mmio()->parser);
  ParserVideoWp::Get().FromValue(buffer_start).WriteTo(owner_->mmio()->parser);
  // The read pointer isn't really used unless the output buffer wraps around.
  ParserVideoRp::Get().FromValue(buffer_start).WriteTo(owner_->mmio()->parser);

  // Keeps bytes in the same order as they were input.
  ParserEsControl::Get()
      .ReadFrom(owner_->mmio()->parser)
      .set_video_manual_read_ptr_update(true)
      .set_video_write_endianness(0x7)
      .WriteTo(owner_->mmio()->parser);
}

void Parser::SyncFromDecoderInstance(DecoderInstance* instance) {
  StreamBuffer* buffer = instance->stream_buffer();
  uint32_t buffer_phys_address = truncate_to_32(buffer->buffer().phys_base());
  size_t buffer_size = buffer->buffer().size();
  ZX_DEBUG_ASSERT(buffer_size <= std::numeric_limits<uint32_t>::max());
  uint32_t read_offset = instance->core()->GetReadOffset();
  uint32_t write_offset = instance->core()->GetStreamInputOffset();
  SyncFromBufferParameters(buffer_phys_address, buffer_size, read_offset, write_offset);
}

void Parser::SyncToDecoderInstance(DecoderInstance* instance) {
  // The ParserVideoWp is the only ringbuffer register that should by changed by the process of
  // parsing.
  instance->core()->UpdateWritePointer(
      ParserVideoWp::Get().ReadFrom(owner_->mmio()->parser).reg_value());
}

void Parser::SyncFromBufferParameters(uint32_t buffer_phys_address, uint32_t buffer_size,
                                      uint32_t read_offset, uint32_t write_offset) {
  // Sync start and end pointers every time so using the same parser with multiple decoder instances
  // and/or for multiple purposes is less error-prone.
  ParserVideoStartPtr::Get().FromValue(buffer_phys_address).WriteTo(owner_->mmio()->parser);
  ParserVideoEndPtr::Get()
      .FromValue(buffer_phys_address + buffer_size - 8)
      .WriteTo(owner_->mmio()->parser);
  ParserVideoRp::Get().FromValue(read_offset + buffer_phys_address).WriteTo(owner_->mmio()->parser);
  ParserVideoWp::Get()
      .FromValue(write_offset + buffer_phys_address)
      .WriteTo(owner_->mmio()->parser);
  // Keeps bytes in the same order as they were input.
  ParserEsControl::Get()
      .ReadFrom(owner_->mmio()->parser)
      .set_video_manual_read_ptr_update(true)
      .set_video_write_endianness(0x7)
      .WriteTo(owner_->mmio()->parser);
}

zx_status_t Parser::ParseVideo(const void* data, uint32_t len) {
#if ZX_DEBUG_ASSERT_IMPLEMENTED
  {
    std::lock_guard<std::mutex> lock(parser_running_lock_);
    ZX_DEBUG_ASSERT(!parser_running_);
  }
#endif

  if (!parser_input_ || io_buffer_size(parser_input_.get(), 0) < len) {
    if (parser_input_) {
      io_buffer_release(parser_input_.get());
      parser_input_ = nullptr;
    }
    parser_input_ = std::make_unique<io_buffer_t>();
    zx_status_t status = io_buffer_init(parser_input_.get(), owner_->bti()->get(), len,
                                        IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
      parser_input_.reset();
      DECODE_ERROR("Failed to create input file");
      return ZX_ERR_NO_MEMORY;
    }
    SetIoBufferName(parser_input_.get(), "ParserInput");
  }

  memcpy(io_buffer_virt(parser_input_.get()), data, len);
  io_buffer_cache_flush(parser_input_.get(), 0, len);
  BarrierAfterFlush();

  return ParseVideoPhysical(io_buffer_phys(parser_input_.get()), len);
}
// The caller of this method must know that the physical range is entirely
// within a VMO that's pinned for at least the duration of this call.
zx_status_t Parser::ParseVideoPhysical(zx_paddr_t paddr, uint32_t len) {
  TRACE_DURATION("media", "Parser::ParseVideoPhysical");
  assert(!owner_->is_parser_gated());
#if ZX_DEBUG_ASSERT_IMPLEMENTED
  {
    std::lock_guard<std::mutex> lock(parser_running_lock_);
    ZX_DEBUG_ASSERT(!parser_running_);
  }
#endif

  PfifoRdPtr::Get().FromValue(0).WriteTo(owner_->mmio()->parser);
  PfifoWrPtr::Get().FromValue(0).WriteTo(owner_->mmio()->parser);

  // es_pack_size seems to be the amount of data that will be just copied through without attempting
  // to search for a start code.
  ParserControl::Get()
      .ReadFrom(owner_->mmio()->parser)
      .set_es_pack_size(len)
      .WriteTo(owner_->mmio()->parser);
  ParserControl::Get()
      .ReadFrom(owner_->mmio()->parser)
      .set_type(0)
      .set_write(true)
      .set_command(ParserControl::kAutoSearch)
      .WriteTo(owner_->mmio()->parser);

  ParserFetchAddr::Get().FromValue(truncate_to_32(paddr)).WriteTo(owner_->mmio()->parser);
  ParserFetchCmd::Get().FromValue(0).set_len(len).set_fetch_endian(7).WriteTo(
      owner_->mmio()->parser);

  // The parser finished interrupt shouldn't be signalled until after
  // es_pack_size data has been read.  The parser cancellation bit should not
  // be set because that bit is never set while parser_running_ is false
  // (ignoring transients while under parser_running_lock_).
  ZX_ASSERT(ZX_ERR_TIMED_OUT == parser_finished_event_.wait_one(ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1,
                                                                zx::time(), nullptr));

  {
    std::lock_guard<std::mutex> lock(parser_running_lock_);
    parser_running_ = true;
  }

  // This data is after es_pack_size, so the parser will search for the search pattern in it.
  ParserFetchAddr::Get()
      .FromValue(truncate_to_32(io_buffer_phys(&search_pattern_)))
      .WriteTo(owner_->mmio()->parser);
  ParserFetchCmd::Get()
      .FromValue(0)
      .set_len(io_buffer_size(&search_pattern_, 0))
      .set_fetch_endian(7)
      .WriteTo(owner_->mmio()->parser);

  return ZX_OK;
}

void Parser::TryStartCancelParsing() {
  {
    std::lock_guard<std::mutex> lock(parser_running_lock_);
    if (!parser_running_) {
      return;
    }
    // Regardless of whether this actually causes WaitForParsingCompleted() to
    // stop early, ZX_USER_SIGNAL_1 will become non-signaled when
    // parser_running_ goes back to false.
    parser_finished_event_.signal(0, ZX_USER_SIGNAL_1);
  }
}

zx_status_t Parser::WaitForParsingCompleted(zx_duration_t deadline) {
  TRACE_DURATION("media", "Parser::WaitForParsingCompleted");
  {
    std::lock_guard<std::mutex> lock(parser_running_lock_);
    ZX_DEBUG_ASSERT(parser_running_);
  }
  zx_signals_t observed = 0;
  zx_status_t status = parser_finished_event_.wait_one(
      ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1, zx::deadline_after(zx::duration(deadline)), &observed);
  if (status != ZX_OK) {
    LOG(ERROR, "parser_finished_event_.wait_one failed status: %d observed %x", status, observed);
    return status;
  }
  if (observed & ZX_USER_SIGNAL_1) {
    // Reporting interruption wins if both bits are observed.
    //
    // The CancelParsing() will clear both ZX_USER_SIGNAL_0 (whether set or not)
    // and ZX_USER_SIGNAL_1.
    //
    // The caller must still call CancelParsing(), as with any error returned
    // from this method.
    LOG(DEBUG, "observed & ZX_USER_SIGNAL_1");
    return ZX_ERR_CANCELED;
  }

  // Observed reports _all_ the signals, so only check the one we know is
  // supposed to be set in observed at this point.
  ZX_DEBUG_ASSERT(observed & ZX_USER_SIGNAL_0);
  std::lock_guard<std::mutex> lock(parser_running_lock_);
  parser_running_ = false;
  // ZX_USER_SIGNAL_1 must be un-signaled while parser_running_ is false.
  parser_finished_event_.signal(ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1, 0);
  // Ensure the parser finishes before parser_input_ is written into again or
  // released. dsb is needed instead of the dmb we get from the mutex.
  BarrierBeforeRelease();
  return ZX_OK;
}

void Parser::CancelParsing() {
  std::lock_guard<std::mutex> lock(parser_running_lock_);
  if (!parser_running_) {
    return;
  }
  assert(!owner_->is_parser_gated());

  LOG(DEBUG, "Parser cancelled");
  parser_running_ = false;

  ParserFetchCmd::Get().FromValue(0).WriteTo(owner_->mmio()->parser);
  // Ensure the parser finishes before parser_input_ is written into again or
  // released. dsb is needed instead of the dmb we get from the mutex.
  BarrierBeforeRelease();
  // Clear the parser interrupt to ensure that if the parser happened to
  // finish before the ParserFetchCmd was processed that the finished event
  // won't be signaled accidentally for the next parse.
  auto status = ParserIntStatus::Get().ReadFrom(owner_->mmio()->parser);
  // Writing 1 to a bit clears it.
  status.WriteTo(owner_->mmio()->parser);
  // ZX_USER_SIGNAL_1 must be un-signaled while parser_running_ is false.
  parser_finished_event_.signal(ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1, 0);
}
