// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vp9_decoder.h"

#include "firmware_blob.h"
#include "macros.h"

using HevcDecStatusReg = HevcAssistScratch0;
using HevcRpmBuffer = HevcAssistScratch1;
using HevcShortTermRps = HevcAssistScratch2;
using Vp9AdaptProbReg = HevcAssistScratch3;
using Vp9MmuMapBuffer = HevcAssistScratch4;
using HevcPpsBuffer = HevcAssistScratch5;
using HevcSaoUp = HevcAssistScratch6;
using HevcStreamSwapBuffer = HevcAssistScratch7;
using HevcStreamSwapBuffer2 = HevcAssistScratch8;
using Vp9ProbSwapBuffer = HevcAssistScratch9;
using Vp9CountSwapBuffer = HevcAssistScratchA;
using Vp9SegMapBuffer = HevcAssistScratchB;
using HevcScaleLut = HevcAssistScratchD;
using HevcLmemDumpAdr = HevcAssistScratchF;
using DecodeMode = HevcAssistScratchJ;
using HevcStreamSwapTest = HevcAssistScratchL;
using HevcWaitFlag = HevcAssistScratchE;
using NalSearchCtl = HevcAssistScratchI;
using DecodeStopPos = HevcAssistScratchK;
using HevcDecodeCount = HevcAssistScratchM;
using HevcDecodeSize = HevcAssistScratchN;

using DebugReg1 = HevcAssistScratchG;

void Vp9Decoder::BufferAllocator::Register(WorkingBuffer* buffer) {
  buffers_.push_back(buffer);
}

zx_status_t Vp9Decoder::BufferAllocator::AllocateBuffers(
    VideoDecoder::Owner* owner) {
  for (auto* buffer : buffers_) {
    zx_status_t status =
        io_buffer_init(&buffer->buffer(), owner->bti(), buffer->size(),
                       IO_BUFFER_CONTIG | IO_BUFFER_RW);
    if (status != ZX_OK) {
      DECODE_ERROR("VP9 working buffer allocation failed: %d\n", status);
      return status;
    }
    io_buffer_cache_flush_invalidate(&buffer->buffer(), 0, buffer->size());
  }
  return ZX_OK;
}

Vp9Decoder::WorkingBuffer::WorkingBuffer(BufferAllocator* allocator,
                                         size_t size)
    : size_(size) {
  allocator->Register(this);
}

Vp9Decoder::WorkingBuffer::~WorkingBuffer() { io_buffer_release(&buffer_); }

uint32_t Vp9Decoder::WorkingBuffer::addr32() {
  return truncate_to_32(io_buffer_phys(&buffer_));
}

Vp9Decoder::~Vp9Decoder() {
  owner_->core()->StopDecoding();
  owner_->core()->WaitForIdle();
}

zx_status_t Vp9Decoder::Initialize() {
  uint8_t* firmware;
  uint32_t firmware_size;
  zx_status_t status = owner_->firmware_blob()->GetFirmwareData(
      FirmwareBlob::FirmwareType::kVp9Mmu, &firmware, &firmware_size);
  if (status != ZX_OK)
    return status;

  status = owner_->core()->LoadFirmware(firmware, firmware_size);
  if (status != ZX_OK)
    return status;

  status = working_buffers_.AllocateBuffers(owner_);
  if (status != ZX_OK)
    return status;

  HevcRpmBuffer::Get()
      .FromValue(working_buffers_.rpm.addr32())
      .WriteTo(owner_->dosbus());
  HevcShortTermRps::Get()
      .FromValue(working_buffers_.short_term_rps.addr32())
      .WriteTo(owner_->dosbus());
  HevcPpsBuffer::Get()
      .FromValue(working_buffers_.picture_parameter_set.addr32())
      .WriteTo(owner_->dosbus());
  HevcStreamSwapBuffer::Get()
      .FromValue(working_buffers_.swap.addr32())
      .WriteTo(owner_->dosbus());
  HevcStreamSwapBuffer2::Get()
      .FromValue(working_buffers_.swap2.addr32())
      .WriteTo(owner_->dosbus());
  HevcLmemDumpAdr::Get()
      .FromValue(working_buffers_.local_memory_dump.addr32())
      .WriteTo(owner_->dosbus());
  HevcdIppLinebuffBase::Get()
      .FromValue(working_buffers_.ipp_line_buffer.addr32())
      .WriteTo(owner_->dosbus());
  HevcSaoUp::Get()
      .FromValue(working_buffers_.sao_up.addr32())
      .WriteTo(owner_->dosbus());
  HevcScaleLut::Get()
      .FromValue(working_buffers_.scale_lut.addr32())
      .WriteTo(owner_->dosbus());

  if (owner_->device_type() == DeviceType::kG12A) {
    HevcDblkCfgE::Get()
        .FromValue(working_buffers_.deblock_data2.addr32())
        .WriteTo(owner_->dosbus());
  }
  HevcDblkCfg4::Get()
      .FromValue(working_buffers_.deblock_parameters.addr32())
      .WriteTo(owner_->dosbus());

  HevcDblkCfg5::Get()
      .FromValue(working_buffers_.deblock_data.addr32())
      .WriteTo(owner_->dosbus());

  HevcdMppDecompCtl1::Get().FromValue(0).set_paged_mode(1).WriteTo(
      owner_->dosbus());
  HevcdMppDecompCtl2::Get().FromValue(0).WriteTo(owner_->dosbus());

  HevcSaoMmuVh0Addr::Get()
      .FromValue(working_buffers_.mmu_vbh.addr32())
      .WriteTo(owner_->dosbus());
  HevcSaoMmuVh1Addr::Get()
      .FromValue(working_buffers_.mmu_vbh.addr32() +
                 working_buffers_.mmu_vbh.size() / 2)
      .WriteTo(owner_->dosbus());
  HevcSaoCtrl5::Get()
      .ReadFrom(owner_->dosbus())
      .set_use_compressed_header(1)
      .WriteTo(owner_->dosbus());

  Vp9SegMapBuffer::Get()
      .FromValue(working_buffers_.segment_map.addr32())
      .WriteTo(owner_->dosbus());
  Vp9ProbSwapBuffer::Get()
      .FromValue(working_buffers_.probability_buffer.addr32())
      .WriteTo(owner_->dosbus());
  Vp9CountSwapBuffer::Get()
      .FromValue(working_buffers_.count_buffer.addr32())
      .WriteTo(owner_->dosbus());

  if (owner_->device_type() == DeviceType::kG12A) {
    HevcAssistMmuMapAddr::Get()
        .FromValue(working_buffers_.frame_map_mmu.addr32())
        .WriteTo(owner_->dosbus());
  } else {
    Vp9MmuMapBuffer::Get()
        .FromValue(working_buffers_.frame_map_mmu.addr32())
        .WriteTo(owner_->dosbus());
  }

  status = AllocateFrames();
  if (status != ZX_OK)
    return status;

  InitializeHardwarePictureList();
  InitializeParser();

  HevcWaitFlag::Get().FromValue(1).WriteTo(owner_->dosbus());

  // The current firmware uses interrupt 0 to communicate.
  HevcAssistMbox0ClrReg::Get().FromValue(1).WriteTo(owner_->dosbus());
  HevcAssistMbox0Mask::Get().FromValue(1).WriteTo(owner_->dosbus());
  HevcPscaleCtrl::Get().FromValue(0).WriteTo(owner_->dosbus());
  DebugReg1::Get().FromValue(0).WriteTo(owner_->dosbus());
  NalSearchCtl::Get().FromValue(8).WriteTo(owner_->dosbus());

  DecodeStopPos::Get().FromValue(0).WriteTo(owner_->dosbus());

  owner_->core()->StartDecoding();
  return ZX_OK;
}

void Vp9Decoder::HandleInterrupt() { DLOG("Got VP9 interrupt\n"); }

void Vp9Decoder::SetFrameReadyNotifier(FrameReadyNotifier notifier) {
  notifier_ = notifier;
}

Vp9Decoder::Frame::~Frame() { io_buffer_release(&compressed_header); }

zx_status_t Vp9Decoder::AllocateFrames() {
  // The VP9 format need 8 reference pictures, plus keep some extra ones that
  // are available for use later in the pipeline.
  for (uint32_t i = 0; i < 16; i++) {
    auto frame = std::make_unique<Frame>();
    constexpr uint32_t kCompressedHeaderSize = 0x48000;
    zx_status_t status = io_buffer_init_aligned(
        &frame->compressed_header, owner_->bti(), kCompressedHeaderSize, 16,
        IO_BUFFER_CONTIG | IO_BUFFER_RW);
    if (status != ZX_OK) {
      DECODE_ERROR("Alloc buffer error: %d\n", status);
      return status;
    }
    io_buffer_cache_flush_invalidate(&frame->compressed_header, 0,
                                     kCompressedHeaderSize);
    frames_.push_back(std::move(frame));
  }

  return ZX_OK;
}

void Vp9Decoder::InitializeHardwarePictureList() {
  HevcdMppAnc2AxiTblConfAddr::Get()
      .FromValue((1 << 1) | (1 << 2))
      .WriteTo(owner_->dosbus());

  // This table maps "canvas" indices to the compressed headers of reference
  // pictures.
  for (auto& frame : frames_) {
    HevcdMppAnc2AxiTblData::Get()
        .FromValue(
            truncate_to_32(io_buffer_phys(&frame->compressed_header) >> 5))
        .WriteTo(owner_->dosbus());
  }

  HevcdMppAnc2AxiTblConfAddr::Get().FromValue(1).WriteTo(owner_->dosbus());

  // Set all reference picture canvas indices to 0.
  HevcdMppAncCanvasAccconfigAddr::Get().FromValue(1).WriteTo(owner_->dosbus());
  for (uint32_t i = 0; i < 32; ++i) {
    HevcdMppAncCanvasDataAddr::Get().FromValue(0).WriteTo(owner_->dosbus());
  }
}

void Vp9Decoder::InitializeParser() {
  HevcParserIntControl::Get()
      .ReadFrom(owner_->dosbus())
      .set_fifo_ctl(3)
      .set_stream_buffer_empty_amrisc_enable(1)
      .set_stream_fifo_empty_amrisc_enable(1)
      .set_dec_done_int_cpu_enable(1)
      .set_startcode_found_int_cpu_enable(1)
      .set_parser_int_enable(1)
      .WriteTo(owner_->dosbus());
  HevcShiftStatus::Get()
      .ReadFrom(owner_->dosbus())
      .set_emulation_check(0)
      .set_startcode_check(1)
      .WriteTo(owner_->dosbus());
  HevcShiftControl::Get()
      .ReadFrom(owner_->dosbus())
      .set_start_code_protect(0)
      .set_length_zero_startcode(1)
      .set_length_valid_startcode(1)
      .set_sft_valid_wr_position(3)
      .set_emulate_code_length_minus1(2)
      .set_start_code_length_minus1(3)
      .set_stream_shift_enable(1)
      .WriteTo(owner_->dosbus());
  HevcCabacControl::Get().FromValue(0).set_enable(true).WriteTo(
      owner_->dosbus());
  HevcParserCoreControl::Get().FromValue(0).set_clock_enable(true).WriteTo(
      owner_->dosbus());
  HevcDecStatusReg::Get().FromValue(0).WriteTo(owner_->dosbus());

  HevcIqitScalelutWrAddr::Get().FromValue(0).WriteTo(owner_->dosbus());
  for (uint32_t i = 0; i < 1024; i++) {
    HevcIqitScalelutData::Get().FromValue(0).WriteTo(owner_->dosbus());
  }

  HevcStreamSwapTest::Get().FromValue(0).WriteTo(owner_->dosbus());
  enum DecodeModes {
    kDecodeModeSingle =
        (0x80 << 24),  // One decoder, instead of multiple at a time.
  };
  DecodeMode::Get().FromValue(kDecodeModeSingle).WriteTo(owner_->dosbus());
  HevcDecodeSize::Get().FromValue(0).WriteTo(owner_->dosbus());
  HevcDecodeCount::Get().FromValue(0).WriteTo(owner_->dosbus());

  HevcParserCmdWrite::Get().FromValue(1 << 16).WriteTo(owner_->dosbus());

  constexpr uint32_t parser_cmds[] = {
      0x0401, 0x8401, 0x0800, 0x0402, 0x9002, 0x1423, 0x8CC3, 0x1423,
      0x8804, 0x9825, 0x0800, 0x04FE, 0x8406, 0x8411, 0x1800, 0x8408,
      0x8409, 0x8C2A, 0x9C2B, 0x1C00, 0x840F, 0x8407, 0x8000, 0x8408,
      0x2000, 0xA800, 0x8410, 0x04DE, 0x840C, 0x840D, 0xAC00, 0xA000,
      0x08C0, 0x08E0, 0xA40E, 0xFC00, 0x7C00};

  for (uint32_t cmd : parser_cmds) {
    HevcParserCmdWrite::Get().FromValue(cmd).WriteTo(owner_->dosbus());
  }
  HevcParserCmdSkip0::Get().FromValue(0x0000090b).WriteTo(owner_->dosbus());
  HevcParserCmdSkip1::Get().FromValue(0x1b14140f).WriteTo(owner_->dosbus());
  HevcParserCmdSkip2::Get().FromValue(0x001b1910).WriteTo(owner_->dosbus());

  HevcParserIfControl::Get()
      .FromValue(0)
      .set_parser_sao_if_enable(true)
      .set_parser_mpred_if_enable(true)
      .set_parser_scaler_if_enable(true)
      .WriteTo(owner_->dosbus());
  HevcdIppTopCntl::Get().FromValue(0).set_reset_ipp_and_mpp(true).WriteTo(
      owner_->dosbus());
  HevcdIppTopCntl::Get().FromValue(0).set_enable_ipp(true).WriteTo(
      owner_->dosbus());

  if (owner_->device_type() == DeviceType::kG12A) {
    HevcStreamFifoCtl::Get()
        .ReadFrom(owner_->dosbus())
        .set_stream_fifo_hole(true)
        .WriteTo(owner_->dosbus());
  }

  // The input format is <32-bit big-endian length><32-bit big-endian length ^
  // 0xffffffff><00><00><00><01>AMLV, which must be inserted by software ahead
  // of time.
  HevcShiftStartCode::Get().FromValue(0x00000001).WriteTo(owner_->dosbus());
  // Shouldn't matter, since the emulation check is disabled.
  HevcShiftEmulateCode::Get().FromValue(0x00003000).WriteTo(owner_->dosbus());
}
