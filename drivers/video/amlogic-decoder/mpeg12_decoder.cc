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
  io_buffer_release(&workspace_buffer_);
  for (auto& frame : video_frames_) {
    owner_->FreeCanvas(std::move(frame.y_canvas));
    owner_->FreeCanvas(std::move(frame.uv_canvas));
  }
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

  enum { kWorkspaceSize = 2 * (1 << 16) }; // 128 kB

  status = io_buffer_init(&workspace_buffer_, owner_->bti(), kWorkspaceSize,
                          IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to make workspace buffer");
    return status;
  }
  io_buffer_cache_flush(&workspace_buffer_, 0, kWorkspaceSize);

  // The first part of the workspace buffer is used for the CC buffer, which
  // stores metadata that was encoded in the stream.
  enum { kCcBufSize = 5 * 1024 };
  MregCoMvStart::Get()
      .FromValue(truncate_to_32(io_buffer_phys(&workspace_buffer_)) + kCcBufSize)
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

  auto& frame = video_frames_[index].frame;
  frame->width = std::min(width, kMaxWidth);
  frame->height = std::min(height, kMaxHeight);
  if (notifier_)
    notifier_(frame);

  MregBufferOut::Get().FromValue(0).WriteTo(owner_->dosbus());
  // Some returned frames may have been buffered up earlier, so try to return
  // them now that the firmware had a chance to do some work.
  TryReturnFrames();

  if (AvScratchM::Get().ReadFrom(owner_->dosbus()).reg_value() & (1 << 16)) {
    DLOG("ccbuf has new data\n");
  }
}

void Mpeg12Decoder::ReturnFrame(std::shared_ptr<VideoFrame> video_frame) {
  returned_frames_.push_back(video_frame);
  TryReturnFrames();
}

void Mpeg12Decoder::TryReturnFrames() {
  while (!returned_frames_.empty()) {
    std::shared_ptr<VideoFrame> frame = returned_frames_.back();
    assert(frame->index < video_frames_.size());
    assert(video_frames_[frame->index].frame == frame);
    // Return buffer to decoder.
    if (MregBufferIn::Get().ReadFrom(owner_->dosbus()).reg_value() == 0) {
      MregBufferIn::Get().FromValue(frame->index + 1).WriteTo(owner_->dosbus());
    } else {
      // No return slots are free, so give up for now.
      return;
    }
    returned_frames_.pop_back();
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
    frame->index = i;
    io_buffer_cache_flush(&frame->buffer, 0, buffer_size);

    auto y_canvas = owner_->ConfigureCanvas(&frame->buffer, 0, frame->stride,
                                            kMaxHeight, 0, 0);
    auto uv_canvas =
        owner_->ConfigureCanvas(&frame->buffer, frame->uv_plane_offset,
                                frame->stride, kMaxHeight / 2, 0, 0);
    if (!y_canvas || !uv_canvas) {
      DECODE_ERROR("Failed to allocate canvases\n");
      return ZX_ERR_NO_MEMORY;
    }
    AvScratch::Get(i)
        .FromValue(y_canvas->index() | (uv_canvas->index() << 8) |
                   (uv_canvas->index() << 16))
        .WriteTo(owner_->dosbus());
    video_frames_.push_back(
        {std::move(frame), std::move(y_canvas), std::move(uv_canvas)});
  }
  return ZX_OK;
}
