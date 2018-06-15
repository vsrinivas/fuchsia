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

Vp9Decoder::~Vp9Decoder() {}

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
  return ZX_OK;
}
void Vp9Decoder::HandleInterrupt() {}

void Vp9Decoder::SetFrameReadyNotifier(FrameReadyNotifier notifier) {
  notifier_ = notifier;
}
