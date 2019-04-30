// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "amlogic-video.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>
#include <hw/reg.h>
#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>
#include <lib/zx/channel.h>
#include <memory.h>
#include <stdint.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <chrono>
#include <memory>
#include <thread>

#include "device_ctx.h"
#include "device_fidl.h"
#include "hevcdec.h"
#include "local_codec_factory.h"
#include "macros.h"
#include "memory_barriers.h"
#include "mpeg12_decoder.h"
#include "pts_manager.h"
#include "registers.h"
#include "vdec1.h"

// These match the regions exported when the bus device was added.
enum MmioRegion {
  kCbus,
  kDosbus,
  kHiubus,
  kAobus,
  kDmc,
};

enum Interrupt {
  kDemuxIrq,
  kParserIrq,
  kDosMbox0Irq,
  kDosMbox1Irq,
  kDosMbox2Irq,
};

AmlogicVideo::AmlogicVideo() {
  zx::event::create(0, &parser_finished_event_);

  vdec1_core_ = std::make_unique<Vdec1>(this);
  hevc_core_ = std::make_unique<HevcDec>(this);
}

AmlogicVideo::~AmlogicVideo() {
  if (parser_interrupt_handle_) {
    zx_interrupt_destroy(parser_interrupt_handle_.get());
    if (parser_interrupt_thread_.joinable())
      parser_interrupt_thread_.join();
  }
  CancelParsing();
  if (parser_input_) {
    io_buffer_release(parser_input_.get());
    parser_input_.reset();
  }
  if (vdec0_interrupt_handle_) {
    zx_interrupt_destroy(vdec0_interrupt_handle_.get());
    if (vdec0_interrupt_thread_.joinable())
      vdec0_interrupt_thread_.join();
  }
  if (vdec1_interrupt_handle_) {
    zx_interrupt_destroy(vdec1_interrupt_handle_.get());
    if (vdec1_interrupt_thread_.joinable())
      vdec1_interrupt_thread_.join();
  }
  swapped_out_instances_.clear();
  if (core_)
    core_->PowerOff();
  current_instance_.reset();
  core_ = nullptr;
  hevc_core_.reset();
  vdec1_core_.reset();
  io_buffer_release(&search_pattern_);
}

// TODO: Remove once we can add single-instance decoders through
// AddNewDecoderInstance.
void AmlogicVideo::SetDefaultInstance(std::unique_ptr<VideoDecoder> decoder,
                                      bool hevc) {
  DecoderCore* core = hevc ? hevc_core_.get() : vdec1_core_.get();
  assert(!stream_buffer_);
  assert(!current_instance_);
  current_instance_ =
      std::make_unique<DecoderInstance>(std::move(decoder), core);
  video_decoder_ = current_instance_->decoder();
  stream_buffer_ = current_instance_->stream_buffer();
  core_ = core;
  core_->PowerOn();
}

void AmlogicVideo::AddNewDecoderInstance(
    std::unique_ptr<DecoderInstance> instance) {
  swapped_out_instances_.push_back(std::move(instance));
}

void AmlogicVideo::UngateClocks() {
  HhiGclkMpeg0::Get()
      .ReadFrom(hiubus_.get())
      .set_dos(true)
      .WriteTo(hiubus_.get());
  HhiGclkMpeg1::Get()
      .ReadFrom(hiubus_.get())
      .set_u_parser_top(true)
      .set_aiu(0xff)
      .set_demux(true)
      .set_audio_in(true)
      .WriteTo(hiubus_.get());
  HhiGclkMpeg2::Get()
      .ReadFrom(hiubus_.get())
      .set_vpu_interrupt(true)
      .WriteTo(hiubus_.get());
}

void AmlogicVideo::GateClocks() {
  // Keep VPU interrupt enabled, as it's used for vsync by the display.
  HhiGclkMpeg1::Get()
      .ReadFrom(hiubus_.get())
      .set_u_parser_top(false)
      .set_aiu(0)
      .set_demux(false)
      .set_audio_in(false)
      .WriteTo(hiubus_.get());
  HhiGclkMpeg0::Get()
      .ReadFrom(hiubus_.get())
      .set_dos(false)
      .WriteTo(hiubus_.get());
}

void AmlogicVideo::ClearDecoderInstance() {
  std::lock_guard<std::mutex> lock(video_decoder_lock_);
  assert(current_instance_);
  assert(swapped_out_instances_.size() == 0);
  current_instance_.reset();
  core_->PowerOff();
  core_ = nullptr;
  video_decoder_ = nullptr;
  stream_buffer_ = nullptr;
}

void AmlogicVideo::RemoveDecoder(VideoDecoder* decoder) {
  DLOG("Removing decoder: %p\n", decoder);
  std::lock_guard<std::mutex> lock(video_decoder_lock_);
  if (current_instance_ && current_instance_->decoder() == decoder) {
    current_instance_.reset();
    video_decoder_ = nullptr;
    stream_buffer_ = nullptr;
    core_->PowerOff();
    core_ = nullptr;
    TryToReschedule();
    return;
  }
  for (auto it = swapped_out_instances_.begin();
       it != swapped_out_instances_.end(); ++it) {
    if ((*it)->decoder() != decoder)
      continue;
    swapped_out_instances_.erase(it);
    return;
  }
}

zx_status_t AmlogicVideo::AllocateStreamBuffer(StreamBuffer* buffer,
                                               uint32_t size) {
  zx_status_t status = io_buffer_init(buffer->buffer(), bti_.get(), size,
                                      IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to make video fifo: %d", status);
    return status;
  }

  io_buffer_cache_flush(buffer->buffer(), 0,
                        io_buffer_size(buffer->buffer(), 0));
  return ZX_OK;
}

void AmlogicVideo::InitializeStreamInput(bool use_parser) {
  uint32_t buffer_address =
      truncate_to_32(io_buffer_phys(stream_buffer_->buffer()));
  core_->InitializeStreamInput(use_parser, buffer_address,
                               io_buffer_size(stream_buffer_->buffer(), 0));
}

zx_status_t AmlogicVideo::InitializeStreamBuffer(bool use_parser,
                                                 uint32_t size) {
  zx_status_t status = AllocateStreamBuffer(stream_buffer_, size);
  if (status != ZX_OK) {
    return status;
  }

  InitializeStreamInput(use_parser);
  return ZX_OK;
}

std::unique_ptr<CanvasEntry> AmlogicVideo::ConfigureCanvas(
    io_buffer_t* io_buffer, uint32_t offset, uint32_t width, uint32_t height,
    uint32_t wrap, uint32_t blockmode) {
  assert(width % 8 == 0);
  assert(offset % 8 == 0);
  canvas_info_t info;
  info.height = height;
  info.stride_bytes = width;
  info.wrap = wrap;
  info.blkmode = blockmode;
  enum {
    kSwapBytes = 1,
    kSwapWords = 2,
    kSwapDoublewords = 4,
    kSwapQuadwords = 8,
  };
  info.endianness =
      kSwapBytes | kSwapWords |
      kSwapDoublewords;  // 64-bit big-endian to little-endian conversion.
  info.flags = CANVAS_FLAGS_READ | CANVAS_FLAGS_WRITE;

  zx::unowned_vmo vmo(io_buffer->vmo_handle);
  zx::vmo dup_vmo;
  zx_status_t status = vmo->duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to duplicate handle, status: %d\n", status);
    return nullptr;
  }
  uint8_t idx;
  status = amlogic_canvas_config(&canvas_, dup_vmo.release(), offset, &info, &idx);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to configure canvas, status: %d\n", status);
    return nullptr;
  }

  return std::make_unique<CanvasEntry>(this, idx);
}

void AmlogicVideo::FreeCanvas(CanvasEntry* canvas) {
  amlogic_canvas_free(&canvas_, canvas->index());
}

zx_status_t AmlogicVideo::AllocateIoBuffer(io_buffer_t* buffer, size_t size,
                                           uint32_t alignment_log2,
                                           uint32_t flags) {
  return io_buffer_init_aligned(buffer, bti_.get(), size, alignment_log2,
                                flags);
}

// This parser handles MPEG elementary streams.
zx_status_t AmlogicVideo::InitializeEsParser() {
  Reset1Register::Get().FromValue(0).set_parser(true).WriteTo(reset_.get());
  FecInputControl::Get().FromValue(0).WriteTo(demux_.get());
  TsHiuCtl::Get()
      .ReadFrom(demux_.get())
      .set_use_hi_bsf_interface(false)
      .WriteTo(demux_.get());
  TsHiuCtl2::Get()
      .ReadFrom(demux_.get())
      .set_use_hi_bsf_interface(false)
      .WriteTo(demux_.get());
  TsHiuCtl3::Get()
      .ReadFrom(demux_.get())
      .set_use_hi_bsf_interface(false)
      .WriteTo(demux_.get());
  TsFileConfig::Get()
      .ReadFrom(demux_.get())
      .set_ts_hiu_enable(false)
      .WriteTo(demux_.get());
  ParserConfig::Get()
      .FromValue(0)
      .set_pfifo_empty_cnt(10)
      .set_max_es_write_cycle(1)
      .set_max_fetch_cycle(16)
      .WriteTo(parser_.get());
  PfifoRdPtr::Get().FromValue(0).WriteTo(parser_.get());
  PfifoWrPtr::Get().FromValue(0).WriteTo(parser_.get());
  constexpr uint32_t kEsStartCodePattern = 0x00000100;
  constexpr uint32_t kEsStartCodeMask = 0x0000ff00;
  ParserSearchPattern::Get()
      .FromValue(kEsStartCodePattern)
      .WriteTo(parser_.get());
  ParserSearchMask::Get().FromValue(kEsStartCodeMask).WriteTo(parser_.get());

  ParserConfig::Get()
      .FromValue(0)
      .set_pfifo_empty_cnt(10)
      .set_max_es_write_cycle(1)
      .set_max_fetch_cycle(16)
      .set_startcode_width(ParserConfig::kWidth24)
      .set_pfifo_access_width(ParserConfig::kWidth8)
      .WriteTo(parser_.get());

  ParserControl::Get()
      .FromValue(ParserControl::kAutoSearch)
      .WriteTo(parser_.get());

  // Set up output fifo.
  uint32_t buffer_address =
      truncate_to_32(io_buffer_phys(stream_buffer_->buffer()));
  ParserVideoStartPtr::Get().FromValue(buffer_address).WriteTo(parser_.get());
  ParserVideoEndPtr::Get()
      .FromValue(buffer_address + io_buffer_size(stream_buffer_->buffer(), 0) -
                 8)
      .WriteTo(parser_.get());

  ParserEsControl::Get()
      .ReadFrom(parser_.get())
      .set_video_manual_read_ptr_update(false)
      .WriteTo(parser_.get());

  core_->InitializeParserInput();

  // 512 bytes includes some padding to force the parser to read it completely.
  constexpr uint32_t kSearchPatternSize = 512;
  zx_status_t status =
      io_buffer_init(&search_pattern_, bti_.get(), kSearchPatternSize,
                     IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to create search pattern buffer");
    return status;
  }

  uint8_t input_search_pattern[kSearchPatternSize] = {0, 0, 1, 0xff};

  memcpy(io_buffer_virt(&search_pattern_), input_search_pattern,
         kSearchPatternSize);
  io_buffer_cache_flush(&search_pattern_, 0, kSearchPatternSize);

  // This check exists so we can call InitializeEsParser() more than once, when
  // called from CodecImpl (indirectly via a CodecAdapter).
  if (!parser_interrupt_thread_.joinable()) {
    parser_interrupt_thread_ = std::thread([this]() {
      DLOG("Starting parser thread\n");
      while (true) {
        zx_time_t time;
        zx_status_t zx_status =
            zx_interrupt_wait(parser_interrupt_handle_.get(), &time);
        if (zx_status != ZX_OK)
          return;

        std::lock_guard<std::mutex> lock(parser_running_lock_);
        if (!parser_running_)
          continue;
        // Continue holding parser_running_lock_ to ensure a cancel doesn't
        // execute while signaling is happening.
        auto status = ParserIntStatus::Get().ReadFrom(parser_.get());
        // Clear interrupt.
        status.WriteTo(parser_.get());
        DLOG("Got Parser interrupt status %x\n", status.reg_value());
        if (status.start_code_found()) {
          PfifoRdPtr::Get().FromValue(0).WriteTo(parser_.get());
          PfifoWrPtr::Get().FromValue(0).WriteTo(parser_.get());
          parser_finished_event_.signal(0, ZX_USER_SIGNAL_0);
        }
      }
    });
  }

  ParserIntStatus::Get().FromValue(0xffff).WriteTo(parser_.get());
  ParserIntEnable::Get()
      .FromValue(0)
      .set_host_en_start_code_found(true)
      .WriteTo(parser_.get());

  return ZX_OK;
}

zx_status_t AmlogicVideo::ParseVideo(void* data, uint32_t len) {
  {
    std::lock_guard<std::mutex> lock(parser_running_lock_);
    ZX_DEBUG_ASSERT(!parser_running_);
  }
  if (!parser_input_ || io_buffer_size(parser_input_.get(), 0) < len) {
    if (parser_input_) {
      io_buffer_release(parser_input_.get());
      parser_input_ = nullptr;
    }
    parser_input_ = std::make_unique<io_buffer_t>();
    zx_status_t status = io_buffer_init(parser_input_.get(), bti_.get(), len,
                                        IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
      parser_input_.reset();
      DECODE_ERROR("Failed to create input file");
      return ZX_ERR_NO_MEMORY;
    }
  }

  PfifoRdPtr::Get().FromValue(0).WriteTo(parser_.get());
  PfifoWrPtr::Get().FromValue(0).WriteTo(parser_.get());
  ParserControl::Get()
      .ReadFrom(parser_.get())
      .set_es_pack_size(len)
      .WriteTo(parser_.get());
  ParserControl::Get()
      .ReadFrom(parser_.get())
      .set_type(0)
      .set_write(true)
      .set_command(ParserControl::kAutoSearch)
      .WriteTo(parser_.get());

  memcpy(io_buffer_virt(parser_input_.get()), data, len);
  io_buffer_cache_flush(parser_input_.get(), 0, len);

  BarrierAfterFlush();

  ParserFetchAddr::Get()
      .FromValue(truncate_to_32(io_buffer_phys(parser_input_.get())))
      .WriteTo(parser_.get());
  ParserFetchCmd::Get().FromValue(0).set_len(len).set_fetch_endian(7).WriteTo(
      parser_.get());

  // The parser finished interrupt shouldn't be signalled until after
  // es_pack_size data has been read.  The parser cancellation bit should not
  // be set because that bit is never set while parser_running_ is false
  // (ignoring transients while under parser_running_lock_).
  assert(ZX_ERR_TIMED_OUT ==
         parser_finished_event_.wait_one(ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1,
                                         zx::time(), nullptr));

  ParserFetchAddr::Get()
      .FromValue(truncate_to_32(io_buffer_phys(&search_pattern_)))
      .WriteTo(parser_.get());
  ParserFetchCmd::Get()
      .FromValue(0)
      .set_len(io_buffer_size(&search_pattern_, 0))
      .set_fetch_endian(7)
      .WriteTo(parser_.get());

  {
    std::lock_guard<std::mutex> lock(parser_running_lock_);
    parser_running_ = true;
  }

  return ZX_OK;
}

void AmlogicVideo::TryStartCancelParsing() {
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

zx_status_t AmlogicVideo::WaitForParsingCompleted(zx_duration_t deadline) {
  {
    std::lock_guard<std::mutex> lock(parser_running_lock_);
    ZX_DEBUG_ASSERT(parser_running_);
  }
  zx_signals_t observed;
  zx_status_t status = parser_finished_event_.wait_one(
      ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1,
      zx::deadline_after(zx::duration(deadline)), &observed);
  if (status != ZX_OK) {
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

void AmlogicVideo::CancelParsing() {
  std::lock_guard<std::mutex> lock(parser_running_lock_);
  if (!parser_running_) {
    return;
  }

  DECODE_ERROR("Parser cancelled\n");
  parser_running_ = false;

  ParserFetchCmd::Get().FromValue(0).WriteTo(parser_.get());
  // Ensure the parser finishes before parser_input_ is written into again or
  // released. dsb is needed instead of the dmb we get from the mutex.
  BarrierBeforeRelease();
  // Clear the parser interrupt to ensure that if the parser happened to
  // finish before the ParserFetchCmd was processed that the finished event
  // won't be signaled accidentally for the next parse.
  auto status = ParserIntStatus::Get().ReadFrom(parser_.get());
  // Writing 1 to a bit clears it.
  status.WriteTo(parser_.get());
  // ZX_USER_SIGNAL_1 must be un-signaled while parser_running_ is false.
  parser_finished_event_.signal(ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1, 0);
}

zx_status_t AmlogicVideo::ProcessVideoNoParser(const void* data, uint32_t len,
                                               uint32_t* written_out) {
  return ProcessVideoNoParserAtOffset(data, len, core_->GetStreamInputOffset(),
                                      written_out);
}

zx_status_t AmlogicVideo::ProcessVideoNoParserAtOffset(const void* data,
                                                       uint32_t len,
                                                       uint32_t write_offset,
                                                       uint32_t* written_out) {
  uint32_t read_offset = core_->GetReadOffset();
  uint32_t available_space;
  if (read_offset > write_offset) {
    available_space = read_offset - write_offset;
  } else {
    available_space = io_buffer_size(stream_buffer_->buffer(), 0) -
                      write_offset + read_offset;
  }
  // Subtract 8 to ensure the read pointer doesn't become equal to the write
  // pointer, as that means the buffer is empty.
  available_space = available_space > 8 ? available_space - 8 : 0;
  if (!written_out) {
    if (len > available_space) {
      DECODE_ERROR("Video too large\n");
      return ZX_ERR_OUT_OF_RANGE;
    }
  } else {
    len = std::min(len, available_space);
    *written_out = len;
  }

  stream_buffer_->set_data_size(stream_buffer_->data_size() + len);
  uint32_t input_offset = 0;
  while (len > 0) {
    uint32_t write_length = len;
    if (write_offset + len > io_buffer_size(stream_buffer_->buffer(), 0))
      write_length = io_buffer_size(stream_buffer_->buffer(), 0) - write_offset;
    memcpy(static_cast<uint8_t*>(io_buffer_virt(stream_buffer_->buffer())) +
               write_offset,
           static_cast<const uint8_t*>(data) + input_offset, write_length);
    io_buffer_cache_flush(stream_buffer_->buffer(), write_offset, write_length);
    write_offset += write_length;
    if (write_offset == io_buffer_size(stream_buffer_->buffer(), 0))
      write_offset = 0;
    len -= write_length;
    input_offset += write_length;
  }
  BarrierAfterFlush();
  core_->UpdateWritePointer(io_buffer_phys(stream_buffer_->buffer()) +
                            write_offset);
  return ZX_OK;
}

void AmlogicVideo::SwapOutCurrentInstance() {
  ZX_DEBUG_ASSERT(!!current_instance_);
  // FrameWasOutput() is called during handling of kVp9CommandNalDecodeDone on
  // the interrupt thread, which means the decoder HW is currently paused,
  // which means it's ok to save the state before the stop+wait (without any
  // explicit pause before the save here).  The decoder HW remains paused
  // after the save, and makes no further progress until later after the
  // restore.
  if (!current_instance_->input_context()) {
    current_instance_->InitializeInputContext();
    if (core_->InitializeInputContext(current_instance_->input_context()) !=
        ZX_OK) {
      // TODO: exit cleanly
      exit(-1);
    }
  }
  video_decoder_->SetSwappedOut();
  core_->SaveInputContext(current_instance_->input_context());
  core_->StopDecoding();
  core_->WaitForIdle();
  // TODO: Avoid power off if swapping to another instance on the same core.
  core_->PowerOff();
  core_ = nullptr;
  // Round-robin; place at the back of the line.
  swapped_out_instances_.push_back(std::move(current_instance_));
}

void AmlogicVideo::TryToReschedule() {
  DLOG("AmlogicVideo::TryToReschedule\n");
  if (swapped_out_instances_.size() == 0)
    return;

  if (current_instance_ && !current_instance_->decoder()->CanBeSwappedOut()) {
    DLOG("Current instance can't be swapped out\n");
    return;
  }

  // Round-robin; first in line that can be swapped in goes first.
  // TODO: Use some priority mechanism to determine which to swap in.
  auto other_instance = swapped_out_instances_.begin();
  for (; other_instance != swapped_out_instances_.end(); ++other_instance) {
    if ((*other_instance)->decoder()->CanBeSwappedIn()) {
      break;
    }
  }
  if (other_instance == swapped_out_instances_.end()) {
    DLOG("nothing to swap to\n");
    return;
  }
  if (current_instance_)
    SwapOutCurrentInstance();
  current_instance_ = std::move(*other_instance);
  swapped_out_instances_.erase(other_instance);

  SwapInCurrentInstance();
}

void AmlogicVideo::SwapInCurrentInstance() {
  ZX_DEBUG_ASSERT(current_instance_);

  core_ = current_instance_->core();
  video_decoder_ = current_instance_->decoder();
  DLOG("Swapping in %p\n", video_decoder_);
  stream_buffer_ = current_instance_->stream_buffer();
  core()->PowerOn();
  zx_status_t status = video_decoder_->InitializeHardware();
  if (status != ZX_OK) {
    // Probably failed to load the right firmware.
    DECODE_ERROR("Failed to initialize hardware: %d\n", status);
    // TODO: exit cleanly
    exit(-1);
  }
  if (!current_instance_->input_context()) {
    InitializeStreamInput(false);
    core_->InitializeDirectInput();
    // If data has added to the stream buffer before the first swap in(only
    // relevant in tests right now) then ensure the write pointer's updated to
    // that spot.
    // Generally data will only be added after this decoder is swapped in, so
    // RestoreInputContext will handle that state.
    core_->UpdateWritePointer(io_buffer_phys(stream_buffer_->buffer()) +
                              stream_buffer_->data_size() +
                              stream_buffer_->padding_size());
  } else {
    core_->RestoreInputContext(current_instance_->input_context());
  }
  video_decoder_->SwappedIn();
}

fidl::InterfaceHandle<fuchsia::sysmem::Allocator> AmlogicVideo::ConnectToSysmem() {
  fidl::InterfaceHandle<fuchsia::sysmem::Allocator> client_end;
  fidl::InterfaceRequest<fuchsia::sysmem::Allocator> server_end =
      client_end.NewRequest();
  zx_status_t connect_status =
      sysmem_connect(&sysmem_, server_end.TakeChannel().release());
  if (connect_status != ZX_OK) {
    // failure
    return fidl::InterfaceHandle<fuchsia::sysmem::Allocator>();
  }
  return client_end;
}

zx_status_t AmlogicVideo::InitRegisters(zx_device_t* parent) {
  parent_ = parent;

  zx_status_t status =
      device_get_protocol(parent_, ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to get parent protocol");
    return ZX_ERR_NO_MEMORY;
  }

  status = device_get_protocol(parent_, ZX_PROTOCOL_SYSMEM, &sysmem_);
  if (status != ZX_OK) {
    DECODE_ERROR("Could not get SYSMEM protocol\n");
    return status;
  }

  status = device_get_protocol(parent_, ZX_PROTOCOL_AMLOGIC_CANVAS, &canvas_);
  if (status != ZX_OK) {
    DECODE_ERROR("Could not get video CANVAS protocol\n");
    return status;
  }

  pdev_device_info_t info;
  status = pdev_get_device_info(&pdev_, &info);
  if (status != ZX_OK) {
    DECODE_ERROR("pdev_get_device_info failed");
    return status;
  }

  switch (info.pid) {
    case PDEV_PID_AMLOGIC_S912:
      device_type_ = DeviceType::kGXM;
      break;
    case PDEV_PID_AMLOGIC_S905D2:
      device_type_ = DeviceType::kG12A;
      break;
    case PDEV_PID_AMLOGIC_T931:
      device_type_ = DeviceType::kG12B;
      break;
    default:
      DECODE_ERROR("Unknown soc pid: %d\n", info.pid);
      return ZX_ERR_INVALID_ARGS;
  }

  mmio_buffer_t cbus_mmio;
  status = pdev_map_mmio_buffer(&pdev_, kCbus, ZX_CACHE_POLICY_UNCACHED_DEVICE, &cbus_mmio);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed map cbus");
    return ZX_ERR_NO_MEMORY;
  }
  cbus_ = std::make_unique<CbusRegisterIo>(cbus_mmio);
  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev_, kDosbus, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed map dosbus");
    return ZX_ERR_NO_MEMORY;
  }
  dosbus_ = std::make_unique<DosRegisterIo>(mmio);
  status = pdev_map_mmio_buffer(&pdev_, kHiubus, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed map hiubus");
    return ZX_ERR_NO_MEMORY;
  }
  hiubus_ = std::make_unique<HiuRegisterIo>(mmio);
  status = pdev_map_mmio_buffer(&pdev_, kAobus, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed map aobus");
    return ZX_ERR_NO_MEMORY;
  }
  aobus_ = std::make_unique<AoRegisterIo>(mmio);
  status = pdev_map_mmio_buffer(&pdev_, kDmc, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed map dmc");
    return ZX_ERR_NO_MEMORY;
  }
  dmc_ = std::make_unique<DmcRegisterIo>(mmio);
  status = pdev_get_interrupt(&pdev_, kParserIrq, 0,
                              parser_interrupt_handle_.reset_and_get_address());
  if (status != ZX_OK) {
    DECODE_ERROR("Failed get parser interrupt");
    return ZX_ERR_NO_MEMORY;
  }
  status = pdev_get_interrupt(&pdev_, kDosMbox0Irq, 0,
                              vdec0_interrupt_handle_.reset_and_get_address());
  if (status != ZX_OK) {
    DECODE_ERROR("Failed get vdec0 interrupt");
    return ZX_ERR_NO_MEMORY;
  }
  status = pdev_get_interrupt(&pdev_, kDosMbox1Irq, 0,
                              vdec1_interrupt_handle_.reset_and_get_address());
  if (status != ZX_OK) {
    DECODE_ERROR("Failed get vdec interrupt");
    return ZX_ERR_NO_MEMORY;
  }
  status = pdev_get_bti(&pdev_, 0, bti_.reset_and_get_address());
  if (status != ZX_OK) {
    DECODE_ERROR("Failed get bti");
    return ZX_ERR_NO_MEMORY;
  }

  int64_t reset_register_offset = 0x1100 * 4;
  int64_t parser_register_offset = 0;
  int64_t demux_register_offset = 0;
  if (IsDeviceAtLeast(device_type_, DeviceType::kG12A)) {
    // Some portions of the cbus moved in newer versions (TXL and later).
    reset_register_offset = 0x0400 * 4;
    parser_register_offset = (0x3800 - 0x2900) * 4;
    demux_register_offset = (0x1800 - 0x1600) * 4;
  }
  reset_ = std::make_unique<ResetRegisterIo>(cbus_mmio, reset_register_offset);
  parser_ =
      std::make_unique<ParserRegisterIo>(cbus_mmio, parser_register_offset);
  demux_ = std::make_unique<DemuxRegisterIo>(cbus_mmio, demux_register_offset);
  registers_ = std::unique_ptr<MmioRegisters>(new MmioRegisters{
      dosbus_.get(), aobus_.get(), dmc_.get(), hiubus_.get(), reset_.get()});

  firmware_ = std::make_unique<FirmwareBlob>();
  status = firmware_->LoadFirmware(parent_);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed load firmware\n");
    return status;
  }

  return ZX_OK;
}

void AmlogicVideo::InitializeInterrupts() {
  vdec0_interrupt_thread_ = std::thread([this]() {
    while (true) {
      zx_time_t time;
      zx_status_t status =
          zx_interrupt_wait(vdec0_interrupt_handle_.get(), &time);
      if (status != ZX_OK)
        return;
      std::lock_guard<std::mutex> lock(video_decoder_lock_);
      if (video_decoder_)
        video_decoder_->HandleInterrupt();
    }
  });

  vdec1_interrupt_thread_ = std::thread([this]() {
    while (true) {
      zx_time_t time;
      zx_status_t status =
          zx_interrupt_wait(vdec1_interrupt_handle_.get(), &time);
      if (status == ZX_ERR_CANCELED) {
        // expected when zx_interrupt_destroy() is called
        return;
      }
      if (status != ZX_OK) {
        // unexpected errors
        DECODE_ERROR(
            "AmlogicVideo::InitializeInterrupts() zx_interrupt_wait() failed "
            "status: %d\n",
            status);
        return;
      }
      std::lock_guard<std::mutex> lock(video_decoder_lock_);
      if (video_decoder_)
        video_decoder_->HandleInterrupt();
    }
  });
}

zx_status_t AmlogicVideo::InitDecoder() {
  InitializeInterrupts();

  return ZX_OK;
}
