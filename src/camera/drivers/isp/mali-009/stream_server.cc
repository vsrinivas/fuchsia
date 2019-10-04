// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stream_server.h"

#include <memory>

#include <ddktl/protocol/isp.h>
#include <fbl/auto_lock.h>
#include <src/lib/fxl/logging.h>

#include "../modules/dma-mgr.h"
#include "arm-isp-test.h"
#include "arm-isp.h"

namespace {
static const auto kDmaPixelFormat = camera::DmaFormat::PixelType::NV12_YUV;
static const auto kPixelFormat = fuchsia::sysmem::PixelFormatType::NV12;
static const auto kColorSpace = fuchsia::sysmem::ColorSpaceType::SRGB;
static const uint32_t kWidth = 1920;
static const uint32_t kHeight = 1080;
static const uint32_t kBufferCount = 8;
static const uint32_t kFramesToHold = kBufferCount - 2;
}  // namespace

namespace camera {

zx_status_t StreamServer::Create(ArmIspDeviceTester* tester,
                                 std::unique_ptr<StreamServer>* server_out,
                                 fuchsia_sysmem_BufferCollectionInfo* buffers_out) {
  *buffers_out = fuchsia_sysmem_BufferCollectionInfo{};

  auto server = std::make_unique<StreamServer>();
  server->tester_ = tester;

  // Start a loop to handle client messages.
  zx_status_t status = server->loop_.StartThread("isp-stream-server-loop");
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Failed to start loop";
    return status;
  }

  // Construct the buffers to pass to the ISP.
  auto format = DmaFormat(kWidth, kHeight, kDmaPixelFormat, false);
  fuchsia::sysmem::BufferCollectionInfo buffers{};
  buffers.format.image().width = kWidth;
  buffers.format.image().height = kHeight;
  buffers.format.image().layers = 2;
  buffers.format.image().pixel_format.type = kPixelFormat;
  buffers.format.image().color_space.type = kColorSpace;
  buffers.format.image().planes[0].bytes_per_row =
      std::abs(static_cast<int32_t>(format.GetLineOffset()));  // May be 'negative'.
  buffers.format.image().planes[1].bytes_per_row =
      buffers.format.image().planes[0].bytes_per_row / 2;
  buffers.buffer_count = kBufferCount;
  buffers.vmo_size = format.GetImageSize();

  fbl::AutoLock lock(&server->tester_->isp_lock_);
  for (uint32_t i = 0; i < buffers.buffer_count; ++i) {
    status = zx::vmo::create_contiguous(server->tester_->GetBti(), buffers.vmo_size, 0,
                                        &buffers.vmos[i]);
    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status) << "Failed to create vmo";
      return status;
    }
    // Initialize chroma channels to 128 (grayscale).
    uintptr_t chroma = 0;
    size_t map_size = format.GetImageSize() - format.GetBank0Offset();
    status = zx::vmar::root_self()->map(0, buffers.vmos[i], format.GetBank0Offset(), map_size,
                                        ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &chroma);
    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status) << "Error mapping vmo";
      return status;
    }
    memset(reinterpret_cast<void*>(chroma), 128, map_size);
    status = zx::vmar::root_self()->unmap(chroma, map_size);
    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status) << "Error unmapping vmo";
      return status;
    }
  }

  server->buffers_ = std::move(buffers);
  status = server->GetBuffers(buffers_out);
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Error getting writable buffers";
    return status;
  }

  *server_out = std::move(server);

  return ZX_OK;
}

zx_status_t StreamServer::AddClient(zx::channel channel,
                                    fuchsia_sysmem_BufferCollectionInfo* buffers_out) {
  std::unique_ptr<camera::StreamImpl> stream;
  zx_status_t status = camera::StreamImpl::Create(std::move(channel), loop_.dispatcher(), &stream);
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Error creating StreamImpl";
    return status;
  }
  status = GetBuffers(buffers_out);
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Error getting read-only buffers";
    return status;
  }
  FXL_LOG(INFO) << "Client " << next_stream_id_ << " connected.";
  streams_[next_stream_id_++] = std::move(stream);
  return ZX_OK;
}

zx_status_t StreamServer::GetBuffers(fuchsia_sysmem_BufferCollectionInfo* buffers_out) {
  buffers_out->format.image.width = buffers_.format.image().width;
  buffers_out->format.image.height = buffers_.format.image().height;
  buffers_out->format.image.layers = buffers_.format.image().layers;
  buffers_out->format.image.pixel_format.type =
      static_cast<uint32_t>(buffers_.format.image().pixel_format.type);
  buffers_out->format.image.color_space.type =
      static_cast<uint32_t>(buffers_.format.image().color_space.type);
  memcpy(buffers_out->format.image.planes, buffers_.format.image().planes.data(),
         sizeof(buffers_out->format.image.planes));
  buffers_out->buffer_count = buffers_.buffer_count;
  buffers_out->vmo_size = buffers_.vmo_size;
  std::vector<zx::vmo> vmos(buffers_.buffer_count);
  for (uint32_t i = 0; i < buffers_.buffer_count; ++i) {
    zx::vmo vmo;
    zx_status_t status = buffers_.vmos[i].duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status) << "Failed to duplicate VMO";
      return status;
    }
    vmos[i] = std::move(vmo);
  }
  for (uint32_t i = 0; i < vmos.size(); ++i) {
    buffers_out->vmos[i] = vmos[i].release();
  }
  return ZX_OK;
}

void StreamServer::FrameAvailable(uint32_t id) {
  // Clean up any disconnected clients.
  std::unordered_set<uint32_t> disconnected_client_ids;
  for (const auto& stream : streams_) {
    if (!stream.second->IsBound()) {
      disconnected_client_ids.insert(stream.first);
    }
  }
  for (auto id : disconnected_client_ids) {
    FXL_LOG(INFO) << "Client " << id << " disconnected.";
    streams_.erase(id);
  }

  // Release any unused frames back to the ISP.
  uint32_t buffer_refs[kBufferCount]{};
  for (const auto& stream : streams_) {
    const auto& buffer_ids = stream.second->GetOutstandingBuffers();
    if (buffer_ids.size() >= kFramesToHold) {
      FXL_LOG(WARNING) << "Client " << stream.first
                       << " is holding too many buffer references and stalling other clients.";
    }
    for (const auto buffer_id : buffer_ids) {
      ++buffer_refs[buffer_id];
    }
  }
  fbl::AutoLock lock(&tester_->isp_lock_);
  for (uint32_t i = 0; i < kBufferCount; ++i) {
    auto it = read_locked_buffers_.find(i);
    if (buffer_refs[i] == 0 && it != read_locked_buffers_.end()) {
      tester_->isp_->ReleaseFrame(i, STREAM_TYPE_FULL_RESOLUTION);
      read_locked_buffers_.erase(it);
    }
  }

  // If clients are collectively holding too many frames, immediately return the buffer to the ISP.
  if (read_locked_buffers_.size() >= kFramesToHold) {
    FXL_LOG(WARNING)
        << "Clients are collectively holding too many buffers. Frames will be dropped.";
    tester_->isp_->ReleaseFrame(id, STREAM_TYPE_FULL_RESOLUTION);
    return;
  }

  // Otherwise, hand it off to the clients.
  read_locked_buffers_.insert(id);
  for (const auto& stream : streams_) {
    stream.second->FrameAvailable(id);
  }
}

}  // namespace camera
