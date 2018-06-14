// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mpeg12_decoder.h"

#include "firmware_blob.h"
#include "macros.h"

using MregSeqInfo = AvScratch4;
using MregPicInfo = AvScratch5;
using MregPicWidth = AvScratch6;
using MregPicHeight = AvScratch7;

// MregBufferIn is used to return buffers to the firmware.
using MregBufferIn = AvScratch8;

// MregBufferOut receives the index of the newest decoded frame from the
// firmware.
using MregBufferOut = AvScratch9;

using MregCmd = AvScratchA;
using MregCoMvStart = AvScratchB;
using MregErrorCount = AvScratchC;

// This is the byte offset within the compressed stream of the data used for the
// currently decoded frame. It can be used to find the PTS.
using MregFrameOffset = AvScratchD;

// MregWaitBuffer is 1 if the hardware is waiting for a buffer to be returned
// before decoding a new frame.
using MregWaitBuffer = AvScratchE;
using MregFatalError = AvScratchF;

Mpeg12Decoder::~Mpeg12Decoder() {
  owner_->core()->StopDecoding();
  owner_->core()->WaitForIdle();
  io_buffer_release(&cc_buffer_);
}

void Mpeg12Decoder::SetFrameReadyNotifier(FrameReadyNotifier notifier) {
  notifier_ = notifier;
}

void Mpeg12Decoder::ResetHardware() {
  auto old_vld = PowerCtlVld::Get().ReadFrom(owner_->dosbus());
  DosSwReset0::Get()
      .FromValue((1 << 7) | (1 << 6) | (1 << 4))
      .WriteTo(owner_->dosbus());
  DosSwReset0::Get().FromValue(0).WriteTo(owner_->dosbus());

  // Reads are used to give the hardware time to finish the operation.
  for (uint32_t i = 0; i < 3; i++) {
    DosSwReset0::Get().ReadFrom(owner_->dosbus());
  }

  DosSwReset0::Get()
      .FromValue((1 << 7) | (1 << 6) | (1 << 4))
      .WriteTo(owner_->dosbus());
  DosSwReset0::Get().FromValue(0).WriteTo(owner_->dosbus());

  DosSwReset0::Get().FromValue((1 << 9) | (1 << 8)).WriteTo(owner_->dosbus());
  DosSwReset0::Get().FromValue(0).WriteTo(owner_->dosbus());

  // Reads are used to give the hardware time to finish the operation.
  for (uint32_t i = 0; i < 3; i++) {
    DosSwReset0::Get().ReadFrom(owner_->dosbus());
  }

  MdecSwReset::Get().FromValue(1 << 7).WriteTo(owner_->dosbus());
  MdecSwReset::Get().FromValue(0).WriteTo(owner_->dosbus());

  old_vld.WriteTo(owner_->dosbus());
}

zx_status_t Mpeg12Decoder::Initialize() {
  uint8_t* data;
  uint32_t firmware_size;

  zx_status_t status = owner_->firmware_blob()->GetFirmwareData(
      FirmwareBlob::FirmwareType::kMPEG12, &data, &firmware_size);
  if (status != ZX_OK)
    return status;
  status = owner_->core()->LoadFirmware(data, firmware_size);
  if (status != ZX_OK)
    return status;

  ResetHardware();

  status = InitializeVideoBuffers();
  if (status != ZX_OK)
    return status;

  enum { kCcBufSize = 5 * 1024 };

  status = io_buffer_init(&cc_buffer_, owner_->bti(), kCcBufSize,
                          IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to make cc buffer");
    return status;
  }
  MregCoMvStart::Get()
      .FromValue(truncate_to_32(io_buffer_phys(&cc_buffer_)) + kCcBufSize)
      .WriteTo(owner_->dosbus());

  Mpeg12Reg::Get().FromValue(0).WriteTo(owner_->dosbus());
  PscaleCtrl::Get().FromValue(0).WriteTo(owner_->dosbus());
  PicHeadInfo::Get().FromValue(0x380).WriteTo(owner_->dosbus());
  M4ControlReg::Get().FromValue(0).WriteTo(owner_->dosbus());
  VdecAssistMbox1ClrReg::Get().FromValue(1).WriteTo(owner_->dosbus());
  MregBufferIn::Get().FromValue(0).WriteTo(owner_->dosbus());
  MregBufferOut::Get().FromValue(0).WriteTo(owner_->dosbus());

  // This is the frame size if it's known, or 0 otherwise.
  MregCmd::Get().FromValue(0).WriteTo(owner_->dosbus());
  MregErrorCount::Get().FromValue(0).WriteTo(owner_->dosbus());
  MregFatalError::Get().FromValue(0).WriteTo(owner_->dosbus());
  MregWaitBuffer::Get().FromValue(0).WriteTo(owner_->dosbus());
  MdecPicDcCtrl::Get()
      .ReadFrom(owner_->dosbus())
      .set_nv12_output(true)
      .WriteTo(owner_->dosbus());

  owner_->core()->StartDecoding();

  return ZX_OK;
}

// Firmware assumes 8 output buffers.
constexpr uint32_t kBuffers = 8;

// Maximum MPEG2 values
constexpr uint32_t kMaxWidth = 1920;
constexpr uint32_t kMaxHeight = 1152;

void Mpeg12Decoder::HandleInterrupt() {
  VdecAssistMbox1ClrReg::Get().FromValue(1).WriteTo(owner_->dosbus());
  auto bufferout = MregBufferOut::Get().ReadFrom(owner_->dosbus());
  auto info = MregPicInfo::Get().ReadFrom(owner_->dosbus());
  auto offset = MregFrameOffset::Get().ReadFrom(owner_->dosbus());

  // Assume frame is progressive.
  uint32_t index = ((bufferout.reg_value() & 0xf) - 1) & (kBuffers - 1);

  uint32_t width = MregPicWidth::Get().ReadFrom(owner_->dosbus()).reg_value();
  uint32_t height = MregPicHeight::Get().ReadFrom(owner_->dosbus()).reg_value();
  DLOG(
      "Received buffer index: %d info: %x, offset: %x, width: %d, height: %d\n",
      index, info.reg_value(), offset.reg_value(), width, height);

  auto& frame = video_frames_[index];
  frame->width = std::min(width, kMaxWidth);
  frame->height = std::min(height, kMaxHeight);
  if (notifier_)
    notifier_(frame.get());

  MregBufferOut::Get().FromValue(0).WriteTo(owner_->dosbus());

  // Return buffer to decoder.
  if (MregBufferIn::Get().ReadFrom(owner_->dosbus()).reg_value() == 0) {
    MregBufferIn::Get().FromValue(index + 1).WriteTo(owner_->dosbus());
  }
}

zx_status_t Mpeg12Decoder::InitializeVideoBuffers() {
  for (uint32_t i = 0; i < kBuffers; i++) {
    // These have to be allocated before the size of the video is known, so
    // they have to be big enough to contain every possible video.
    size_t buffer_size = kMaxWidth * kMaxHeight * 3 / 2;
    auto frame = std::make_unique<VideoFrame>();
    zx_status_t status =
        io_buffer_init(&frame->buffer, owner_->bti(), buffer_size,
                       IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
      DECODE_ERROR("Failed to make frame: %d\n", status);
      return status;
    }

    frame->stride = kMaxWidth;
    frame->uv_plane_offset = kMaxWidth * kMaxHeight;
    io_buffer_cache_flush(&frame->buffer, 0, buffer_size);

    uint32_t buffer_start = truncate_to_32(io_buffer_phys(&frame->buffer));
    // Try to avoid overlapping with any framebuffers.
    // TODO(ZX-2154): Use canvas driver to allocate indices.
    constexpr uint32_t kCanvasOffset = 5;
    // Linux code uses 32x32 block size (and different endianness) for these for
    // some reason.
    uint32_t y_index = 2 * i + kCanvasOffset;
    uint32_t uv_index = y_index + 1;
    // NV12 output format.
    owner_->ConfigureCanvas(y_index, buffer_start, kMaxWidth, kMaxHeight, 0,
                            DmcCavLutDatah::kBlockModeLinear);
    owner_->ConfigureCanvas(uv_index, buffer_start + frame->uv_plane_offset,
                            kMaxWidth, kMaxHeight / 2, 0,
                            DmcCavLutDatah::kBlockModeLinear);
    AvScratch::Get(i)
        .FromValue(y_index | (uv_index << 8) | (uv_index << 16))
        .WriteTo(owner_->dosbus());
    video_frames_.push_back(std::move(frame));
  }
  return ZX_OK;
}
