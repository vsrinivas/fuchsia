// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dma-mgr.h"

#include <lib/syslog/global.h>

#include <cstdint>

#include "../mali-009/pingpong_regs.h"
#include "dma-format.h"

namespace camera {

constexpr auto TAG = "arm-isp";

zx_status_t DmaManager::Create(const zx::bti& bti, const ddk::MmioView& isp_mmio_local,
                               DmaManager::Stream stream_type, std::unique_ptr<DmaManager>* out) {
  *out = std::make_unique<DmaManager>(stream_type, isp_mmio_local);

  zx_status_t status = bti.duplicate(ZX_RIGHT_SAME_RIGHTS, &(*out)->bti_);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, TAG, "%s: Unable to duplicate bti for DmaManager \n", __func__);
    return status;
  }

  return ZX_OK;
}

auto DmaManager::GetPrimaryMisc() {
  if (stream_type_ == Stream::Downscaled) {
    return ping::DownScaled::Primary::DmaWriter_Misc::Get();
  }
  return ping::FullResolution::Primary::DmaWriter_Misc::Get();
}

auto DmaManager::GetUvMisc() {
  if (stream_type_ == Stream::Downscaled) {
    return ping::DownScaled::Uv::DmaWriter_Misc::Get();
  }
  return ping::FullResolution::Uv::DmaWriter_Misc::Get();
}

auto DmaManager::GetPrimaryBank0() {
  if (stream_type_ == Stream::Downscaled) {
    return ping::DownScaled::Primary::DmaWriter_Bank0Base::Get();
  }
  return ping::FullResolution::Primary::DmaWriter_Bank0Base::Get();
}

auto DmaManager::GetUvBank0() {
  if (stream_type_ == Stream::Downscaled) {
    return ping::DownScaled::Uv::DmaWriter_Bank0Base::Get();
  }
  return ping::FullResolution::Uv::DmaWriter_Bank0Base::Get();
}

auto DmaManager::GetPrimaryLineOffset() {
  if (stream_type_ == Stream::Downscaled) {
    return ping::DownScaled::Primary::DmaWriter_LineOffset::Get();
  }
  return ping::FullResolution::Primary::DmaWriter_LineOffset::Get();
}

auto DmaManager::GetUvLineOffset() {
  if (stream_type_ == Stream::Downscaled) {
    return ping::DownScaled::Uv::DmaWriter_LineOffset::Get();
  }
  return ping::FullResolution::Uv::DmaWriter_LineOffset::Get();
}

auto DmaManager::GetPrimaryActiveDim() {
  if (stream_type_ == Stream::Downscaled) {
    return ping::DownScaled::Primary::DmaWriter_ActiveDim::Get();
  }
  return ping::FullResolution::Primary::DmaWriter_ActiveDim::Get();
}

auto DmaManager::GetUvActiveDim() {
  if (stream_type_ == Stream::Downscaled) {
    return ping::DownScaled::Uv::DmaWriter_ActiveDim::Get();
  }
  return ping::FullResolution::Uv::DmaWriter_ActiveDim::Get();
}

auto DmaManager::GetPrimaryFrameCount() {
  if (stream_type_ == Stream::Downscaled) {
    return ping::DownScaled::Primary::DmaWriter_FrameCount::Get();
  }
  return ping::FullResolution::Primary::DmaWriter_FrameCount::Get();
}

auto DmaManager::GetUvFrameCount() {
  if (stream_type_ == Stream::Downscaled) {
    return ping::DownScaled::Uv::DmaWriter_FrameCount::Get();
  }
  return ping::FullResolution::Uv::DmaWriter_FrameCount::Get();
}

auto DmaManager::GetPrimaryFailures() {
  if (stream_type_ == Stream::Downscaled) {
    return ping::DownScaled::Primary::DmaWriter_Failures::Get();
  }
  return ping::FullResolution::Primary::DmaWriter_Failures::Get();
}

auto DmaManager::GetUvFailures() {
  if (stream_type_ == Stream::Downscaled) {
    return ping::DownScaled::Uv::DmaWriter_Failures::Get();
  }
  return ping::FullResolution::Uv::DmaWriter_Failures::Get();
}

void DmaManager::PrintStatus(ddk::MmioBuffer* mmio) {
  printf("%s DMA Status:\n   Primary:\n",
         (stream_type_ == Stream::Downscaled) ? "Downscaled" : "Full Resolution");
  GetPrimaryFrameCount().ReadFrom(mmio).Print();
  GetPrimaryFailures().ReadFrom(mmio).Print();
  printf("   Secondary:\n");
  GetUvFrameCount().ReadFrom(mmio).Print();
  GetUvFailures().ReadFrom(mmio).Print();
}

zx_status_t DmaManager::Configure(
    fuchsia_sysmem_BufferCollectionInfo_2 buffer_collection,
    const fuchsia_sysmem_ImageFormat_2& image_format,
    fit::function<void(fuchsia_camera_FrameAvailableEvent)> frame_available_callback) {
  current_format_ = DmaFormat(image_format);
  // TODO(CAM-54): Provide a way to dump the previous set of write locked
  // buffers.
  write_locked_buffers_.clear();

  if (current_format_->GetImageSize() > buffer_collection.settings.buffer_settings.size_bytes) {
    FX_LOGF(ERROR, TAG, "Buffer size (%lu) is less than image size (%lu)!\n",
            buffer_collection.settings.buffer_settings.size_bytes, current_format_->GetImageSize());
    return ZX_ERR_INTERNAL;
  }
  if (buffer_collection.buffer_count > countof(buffer_collection.buffers)) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx::vmo vmos[countof(buffer_collection.buffers)];
  for (uint32_t i = 0; i < buffer_collection.buffer_count; ++i) {
    vmos[i] = zx::vmo(buffer_collection.buffers[i].vmo);
  }
  // Pin the buffers
  zx_status_t status = buffers_.Init(vmos, buffer_collection.buffer_count);
  if (status != ZX_OK) {
    FX_LOG(ERROR, TAG, "Unable to initialize buffers for DmaManager");
    return status;
  }
  // Release the vmos so that the buffer collection could be reused.
  for (uint32_t i = 0; i < buffer_collection.buffer_count; ++i) {
    buffer_collection.buffers[i].vmo = vmos[i].release();
  }
  status =
      buffers_.PinVmos(bti_, fzl::VmoPool::RequireContig::Yes, fzl::VmoPool::RequireLowMem::Yes);
  if (status != ZX_OK) {
    FX_LOG(ERROR, TAG, "Unable to pin buffers for DmaManager");
    return status;
  }
  frame_available_callback_ = std::move(frame_available_callback);
  return ZX_OK;
}

void DmaManager::Enable() {
  ZX_ASSERT(frame_available_callback_ != nullptr);
  enabled_ = true;
}

void DmaManager::Disable() {
  enabled_ = false;
  // TODO(CAM-54): Provide a way to dump the previous set of write locked
  // buffers.
  write_locked_buffers_.clear();
  // Don't send a callback the next time we get OnNewFrame, because we will not
  // have any previously written frame.
  first_frame_ = true;
}

void DmaManager::OnFrameWritten() {
  // If we have not started streaming, just skip.
  if (!enabled_) {
    return;
  }
  ZX_ASSERT(frame_available_callback_ != nullptr);
  ZX_ASSERT(!write_locked_buffers_.empty());
  fuchsia_camera_FrameAvailableEvent event;
  // If we had a buffer available when we loaded the new frame:
  if (write_locked_buffers_.back()) {
    event.buffer_id = write_locked_buffers_.back()->ReleaseWriteLockAndGetIndex();
    event.frame_status = fuchsia_camera_FrameStatus_OK;
    // TODO(garratt): set metadata
    event.metadata.timestamp = 0;
  } else {
    // We were not able to get a buffer when the frame was loaded. So we just
    // let the client know now that the buffer was full.
    event.buffer_id = 0;
    event.frame_status = fuchsia_camera_FrameStatus_ERROR_BUFFER_FULL;
    event.metadata.timestamp = 0;
  }
  write_locked_buffers_.pop_back();
  frame_available_callback_(event);
}

// Called as one of the later steps when a new frame arrives.
void DmaManager::OnNewFrame() {
  if (!enabled_) {
    return;
  }

  // First, call the callbacks for the previous written frame
  // This assumes that OnFrameWritten will not be called by a seperate interrupt.
  if (first_frame_) {
    first_frame_ = false;
  } else if (write_locked_buffers_.size() > 0) {
    OnFrameWritten();
  }

  // Next, load a frame for the next dma write:
  LoadNewFrame();
}

void DmaManager::LoadNewFrame() {
  // Generally we use LoadFrame to load as many buffers are needed for system operation.
  // We mark first_frame_ false here because we assume that LoadNewFrame will be called the correct
  // number of times to set up the system.
  first_frame_ = false;
  // 1) Get another buffer
  auto buffer = buffers_.LockBufferForWrite();
  uint32_t enable_buffer_write = 0;
  if (buffer) {
    if (buffer_underrun_sequential_count_ > 0) {
      FX_LOGF(INFO, TAG, "DmaManager: buffer underrun recovered - dropped %llu frames",
              buffer_underrun_sequential_count_);
      buffer_underrun_sequential_count_ = 0;
    }

    // 3) Set the DMA address
    auto memory_address = static_cast<uint32_t>(buffer->physical_address());

    // clang-format off
    GetPrimaryBank0().FromValue(0)
      .set_value(memory_address + current_format_->GetBank0Offset())
      .WriteTo(&isp_mmio_local_);
    if (current_format_->HasSecondaryChannel()) {
        GetUvBank0().FromValue(0)
          .set_value(memory_address + current_format_->GetBank0OffsetUv())
          .WriteTo(&isp_mmio_local_);
    }
    // clang-format on

    WriteFormat();
    enable_buffer_write = 1;
  } else {
    // If we run out of buffers, disable write and send the callback for
    // out of buffers:
    constexpr uint32_t kUnderrunReportPeriod = 5 * 30;  // Log every 5 seconds at 30FPS
    if ((buffer_underrun_sequential_count_++ % kUnderrunReportPeriod) == 0) {
      FX_LOGF(WARNING, TAG, "DmaManager: no free buffers - dropped %llu frames",
              buffer_underrun_sequential_count_);
    }
  }

  // 4) Set Write_on
  GetPrimaryMisc()
      .ReadFrom(&isp_mmio_local_)
      .set_frame_write_on(enable_buffer_write)
      .WriteTo(&isp_mmio_local_);
  if (current_format_->HasSecondaryChannel()) {
    GetUvMisc()
        .ReadFrom(&isp_mmio_local_)
        .set_frame_write_on(enable_buffer_write)
        .WriteTo(&isp_mmio_local_);
  }
  // Add buffer to queue of buffers we are writing:
  write_locked_buffers_.push_front(std::move(buffer));
}

zx_status_t DmaManager::ReleaseFrame(uint32_t buffer_index) {
  return buffers_.ReleaseBuffer(buffer_index);
}

void DmaManager::WriteFormat() {
  // Write format to registers
  // clang-format off
    GetPrimaryMisc().ReadFrom(&isp_mmio_local_)
        .set_base_mode(current_format_->GetBaseMode())
        .set_plane_select(current_format_->GetPlaneSelect())
        .WriteTo(&isp_mmio_local_);
    GetPrimaryActiveDim().ReadFrom(&isp_mmio_local_)
        .set_active_width(current_format_->width())
        .set_active_height(current_format_->height())
        .WriteTo(&isp_mmio_local_);
    GetPrimaryLineOffset().ReadFrom(&isp_mmio_local_)
        .set_value(current_format_->GetLineOffset())
        .WriteTo(&isp_mmio_local_);
    if (current_format_->HasSecondaryChannel()) {
        GetUvMisc().ReadFrom(&isp_mmio_local_)
            .set_base_mode(current_format_->GetBaseMode())
            .set_plane_select(current_format_->GetPlaneSelectUv())
            .WriteTo(&isp_mmio_local_);
        GetUvActiveDim().ReadFrom(&isp_mmio_local_)
            .set_active_width(current_format_->width())
            .set_active_height(current_format_->height())
            .WriteTo(&isp_mmio_local_);
        GetUvLineOffset().ReadFrom(&isp_mmio_local_)
            .set_value(current_format_->GetLineOffset())
            .WriteTo(&isp_mmio_local_);
    }
  // clang-format on
}
}  // namespace camera
