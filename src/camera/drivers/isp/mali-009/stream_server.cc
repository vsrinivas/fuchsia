// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stream_server.h"

#include <array>
#include <memory>

#include <ddktl/protocol/isp.h>
#include <fbl/auto_lock.h>

#include "../modules/dma-mgr.h"
#include "src/lib/syslog/cpp/logger.h"

namespace {
static const auto kDmaPixelFormat = camera::DmaFormat::PixelType::NV12_YUV;
static const auto kPixelFormat = fuchsia::sysmem::PixelFormatType::NV12;
static const auto kColorSpace = fuchsia::sysmem::ColorSpaceType::SRGB;
static const uint32_t kWidth = 2176;
static const uint32_t kHeight = 2720;
static const uint32_t kBufferCount = 8;
static const uint32_t kFramesToHold = kBufferCount - 2;
}  // namespace

namespace camera {

constexpr auto kTag = "arm-isp";

zx_status_t StreamServer::Create(zx::bti* bti, std::unique_ptr<StreamServer>* server_out,
                                 fuchsia_sysmem_BufferCollectionInfo_2* buffers_out,
                                 fuchsia_sysmem_ImageFormat_2* format_out) {
  *buffers_out = fuchsia_sysmem_BufferCollectionInfo_2{};
  *format_out = fuchsia_sysmem_ImageFormat_2{};

  auto server = std::make_unique<StreamServer>();

  // Start a loop to handle client messages.
  zx_status_t status = server->loop_.StartThread("isp-stream-server-loop");
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Failed to start loop";
    return status;
  }

  // Construct the buffers to pass to the ISP.
  auto format = DmaFormat(kWidth, kHeight, kDmaPixelFormat, false);
  fuchsia::sysmem::BufferCollectionInfo_2 buffers{};
  fuchsia::sysmem::ImageFormat_2 image_format{};
  image_format.pixel_format.type = kPixelFormat;
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  image_format.color_space.type = kColorSpace;
  image_format.bytes_per_row =
      std::abs(static_cast<int32_t>(format.GetLineOffset()));  // May be 'negative'.
  buffers.buffer_count = kBufferCount;
  buffers.settings.buffer_settings.size_bytes = format.GetImageSize();

  for (uint32_t i = 0; i < buffers.buffer_count; ++i) {
    status = zx::vmo::create_contiguous(*bti, format.GetImageSize(), 0, &buffers.buffers[i].vmo);
    if (status != ZX_OK) {
      FX_PLOGST(ERROR, kTag, status) << "Failed to create vmo";
      return status;
    }
    // Initialize chroma channels to 128 (grayscale).
    uintptr_t chroma = 0;
    status = zx::vmar::root_self()->map(0, buffers.buffers[i].vmo, 0, format.GetImageSize(),
                                        ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &chroma);
    if (status != ZX_OK) {
      FX_PLOGST(ERROR, kTag, status) << "Error mapping vmo";
      return status;
    }
    memset(reinterpret_cast<void*>(chroma), 128, format.GetImageSize());
    status = zx::vmar::root_self()->unmap(chroma, format.GetImageSize());
    if (status != ZX_OK) {
      FX_PLOGST(ERROR, kTag, status) << "Error unmapping vmo";
      return status;
    }
  }

  server->buffers_ = std::move(buffers);
  status = server->GetBuffers(buffers_out);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Error getting writable buffers";
    return status;
  }

  *format_out = *reinterpret_cast<fuchsia_sysmem_ImageFormat_2*>(&image_format);
  *server_out = std::move(server);

  return ZX_OK;
}

zx_status_t StreamServer::AddClient(zx::channel channel,
                                    fuchsia_sysmem_BufferCollectionInfo_2* buffers_out) {
  std::unique_ptr<camera::StreamImpl> stream;
  zx_status_t status = camera::StreamImpl::Create(std::move(channel), loop_.dispatcher(), &stream);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Error creating StreamImpl";
    return status;
  }
  status = GetBuffers(buffers_out);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Error getting read-only buffers";
    return status;
  }
  streams_[next_stream_id_++] = std::move(stream);
  return ZX_OK;
}

zx_status_t StreamServer::GetBuffers(fuchsia_sysmem_BufferCollectionInfo_2* buffers_out) {
  *buffers_out = *reinterpret_cast<fuchsia_sysmem_BufferCollectionInfo_2*>(&buffers_);
  std::vector<zx::vmo> vmos(buffers_.buffer_count);
  for (uint32_t i = 0; i < buffers_.buffer_count; ++i) {
    zx::vmo vmo;
    zx_status_t status = buffers_.buffers[i].vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
    if (status != ZX_OK) {
      FX_PLOGST(ERROR, kTag, status) << "Failed to duplicate VMO";
      return status;
    }
    vmos[i] = std::move(vmo);
  }
  for (uint32_t i = 0; i < vmos.size(); ++i) {
    buffers_out->buffers[i].vmo = vmos[i].release();
  }
  return ZX_OK;
}

void StreamServer::FrameAvailable(uint32_t id, std::list<uint32_t>* out_frames_to_be_released) {
  // Clean up any disconnected clients.
  std::unordered_set<uint32_t> disconnected_client_ids;
  for (const auto& stream : streams_) {
    if (!stream.second->IsBound()) {
      disconnected_client_ids.insert(stream.first);
    }
  }
  for (auto id : disconnected_client_ids) {
    streams_.erase(id);
  }

  // Release any unused frames back to the ISP.
  std::array<uint32_t, kBufferCount> buffer_refs{};
  for (const auto& stream : streams_) {
    const auto& buffer_ids = stream.second->GetOutstandingBuffers();
    if (buffer_ids.size() >= kFramesToHold) {
      FX_LOGST(WARNING, kTag)
          << "Client " << stream.first
          << " is holding too many buffer references and stalling other clients.";
    }
    for (const auto buffer_id : buffer_ids) {
      ++buffer_refs[buffer_id];
    }
  }

  for (uint32_t i = 0; i < kBufferCount; ++i) {
    auto it = read_locked_buffers_.find(i);
    if (buffer_refs[i] == 0 && it != read_locked_buffers_.end()) {
      out_frames_to_be_released->push_back(i);
      read_locked_buffers_.erase(it);
    }
  }

  // If clients are collectively holding too many frames, immediately return the buffer to the ISP.
  if (read_locked_buffers_.size() >= kFramesToHold) {
    FX_LOGST(WARNING, kTag)
        << "Clients are collectively holding too many buffers. Frames will be dropped.";
    out_frames_to_be_released->push_back(id);
    return;
  }

  // Otherwise, hand it off to the clients.
  read_locked_buffers_.insert(id);
  for (const auto& stream : streams_) {
    stream.second->FrameAvailable(id);
  }
}

}  // namespace camera
