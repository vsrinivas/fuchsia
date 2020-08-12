// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/virtual_camera/virtual_camera_impl.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <cmath>
#include <sstream>

namespace camera {

fit::result<std::unique_ptr<VirtualCamera>, zx_status_t> VirtualCamera::Create(
    fidl::InterfaceHandle<fuchsia::sysmem::Allocator> allocator) {
  auto result = VirtualCameraImpl::Create(std::move(allocator));
  if (result.is_error()) {
    return fit::error(result.error());
  }
  return fit::ok(result.take_value());
}

VirtualCameraImpl::VirtualCameraImpl() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

VirtualCameraImpl::~VirtualCameraImpl() {
  async::PostTask(loop_.dispatcher(), fit::bind_member(this, &VirtualCameraImpl::OnDestruction));
  loop_.JoinThreads();
}

static fuchsia::camera3::StreamProperties DefaultStreamProperties() {
  return {.image_format{.pixel_format{.type = fuchsia::sysmem::PixelFormatType::NV12},
                        .coded_width = 1280,
                        .coded_height = 720,
                        .bytes_per_row = 1280,
                        .color_space{.type = fuchsia::sysmem::ColorSpaceType::REC601_NTSC}},
          .frame_rate{.numerator = 30, .denominator = 1},
          .supports_crop_region = false};
}

static fuchsia::sysmem::BufferCollectionConstraints DefaultBufferConstraints() {
  return {.usage = {.cpu = fuchsia::sysmem::cpuUsageWrite},
          .min_buffer_count_for_camping = 2,
          .has_buffer_memory_constraints = true,
          .buffer_memory_constraints{.ram_domain_supported = true},
          .image_format_constraints_count = 1,
          .image_format_constraints = {{{
              .pixel_format = DefaultStreamProperties().image_format.pixel_format,
              .color_spaces_count = 1,
              .color_space = {DefaultStreamProperties().image_format.color_space},
              .min_coded_width = DefaultStreamProperties().image_format.coded_width,
              .max_coded_width = DefaultStreamProperties().image_format.coded_width,
              .min_coded_height = DefaultStreamProperties().image_format.coded_height,
              .max_coded_height = DefaultStreamProperties().image_format.coded_height,
              .coded_width_divisor = 128,
          }}}};
}

constexpr uint32_t kEmbeddedMetadataMagic = 0x5643414D;  // "VCAM"

constexpr size_t kEmbeddedMetadataNumBits =
    CHAR_BIT * (sizeof(kEmbeddedMetadataMagic) + sizeof(fuchsia::camera3::FrameInfo::buffer_index) +
                sizeof(fuchsia::camera3::FrameInfo::frame_counter) +
                sizeof(fuchsia::camera3::FrameInfo::timestamp));

// Embeds the bytes of |value| into the memory specified by |data| by overwriting the least
// significant bit of bytes at fixed offsets into the data stream. Returns the size of |data|
// consumed by embedding the metadata.
template <class T>
static size_t Embed(char* data, T value, size_t total_buffer_size) {
  size_t offset = 0;
  const size_t offset_per_bit = total_buffer_size / kEmbeddedMetadataNumBits;
  for (size_t i = 0; i < sizeof(value) * CHAR_BIT; ++i) {
    data[offset] &= ~1;
    data[offset] |= (value >> i) & 1;
    offset += offset_per_bit;
  }
  return offset;
}

// Reverses the process of Embed, constructing a value from the least significant bits of various
// bytes in |data|. Returns the size of |data| consumed by extracting the metadata.
template <class T>
static size_t Extract(const char* data, T& value, size_t total_buffer_size) {
  value = 0;
  size_t offset = 0;
  const size_t offset_per_bit = total_buffer_size / kEmbeddedMetadataNumBits;
  for (size_t i = 0; i < sizeof(value) * CHAR_BIT; ++i) {
    value |= static_cast<T>(data[offset] & 1) << i;
    offset += offset_per_bit;
  }
  return offset;
}

fit::result<std::unique_ptr<VirtualCamera>, zx_status_t> VirtualCameraImpl::Create(
    fidl::InterfaceHandle<fuchsia::sysmem::Allocator> allocator) {
  auto camera = std::make_unique<VirtualCameraImpl>();

  ZX_ASSERT(camera->allocator_.Bind(std::move(allocator), camera->loop_.dispatcher()) == ZX_OK);
  camera->allocator_.set_error_handler([camera = camera.get()](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "fuchsia.sysmem.Allocator server disconnected.";
    camera->camera_ = nullptr;
  });

  auto stream_result =
      FakeStream::Create(DefaultStreamProperties(),
                         fit::bind_member(camera.get(), &VirtualCameraImpl::OnSetBufferCollection));
  if (stream_result.is_error()) {
    FX_PLOGS(ERROR, stream_result.error()) << "Failed to create fake stream.";
    return fit::error(ZX_ERR_INTERNAL);
  }

  camera->stream_ = stream_result.take_value();

  camera::FakeConfiguration config;
  config.push_back(camera->stream_);
  std::vector<camera::FakeConfiguration> configs;
  configs.push_back(std::move(config));
  auto camera_result = FakeCamera::Create("VirtualCamera", std::move(configs));
  if (camera_result.is_error()) {
    FX_PLOGS(ERROR, camera_result.error()) << "Failed to create fake camera.";
    return fit::error(ZX_ERR_INTERNAL);
  }

  camera->camera_ = camera_result.take_value();

  ZX_ASSERT(camera->loop_.StartThread("Virtual Camera Loop") == ZX_OK);

  camera->interrupt_start_time_ = zx::clock::get_monotonic();
  camera->frame_count_ = 0;
  ZX_ASSERT(async::PostTask(camera->loop_.dispatcher(),
                            fit::bind_member(camera.get(), &VirtualCameraImpl::FrameTick)) ==
            ZX_OK);

  return fit::ok(std::move(camera));
}

fidl::InterfaceRequestHandler<fuchsia::camera3::Device> VirtualCameraImpl::GetHandler() {
  return camera_->GetHandler();
}

fit::result<void, std::string> VirtualCameraImpl::CheckFrame(
    const void* data, size_t size, const fuchsia::camera3::FrameInfo& info) {
  if (size != buffers_->settings.buffer_settings.size_bytes) {
    return fit::error("Data size does not match buffer size.");
  }

  const char* image_data = reinterpret_cast<const char*>(data);

  uint32_t embedded_magic = 0;
  image_data += Extract(image_data, embedded_magic, size);
  if (embedded_magic != kEmbeddedMetadataMagic) {
    std::ostringstream oss;
    oss << "Magic value mismatch: expected " << kEmbeddedMetadataMagic << ", got " << embedded_magic
        << ".";
    return fit::error(oss.str());
  }

  fuchsia::camera3::FrameInfo embedded_info{};

  image_data += Extract(image_data, embedded_info.buffer_index, size);
  if (embedded_info.buffer_index != info.buffer_index) {
    std::ostringstream oss;
    oss << "Buffer index mismatch: data = " << embedded_info.buffer_index
        << ", info = " << info.buffer_index << ".";
    return fit::error(oss.str());
  }

  image_data += Extract(image_data, embedded_info.frame_counter, size);
  if (embedded_info.frame_counter != info.frame_counter) {
    std::ostringstream oss;
    oss << "Frame counter mismatch: data = " << embedded_info.frame_counter
        << ", info = " << info.frame_counter << ".";
    return fit::error(oss.str());
  }

  image_data += Extract(image_data, embedded_info.timestamp, size);
  if (embedded_info.timestamp != info.timestamp) {
    std::ostringstream oss;
    oss << "Timestamp mismatch: data = " << embedded_info.timestamp << ", info = " << info.timestamp
        << ".";
    return fit::error(oss.str());
  }

  return fit::ok();
}

void VirtualCameraImpl::OnDestruction() {
  loop_.Quit();
  for (auto& it : frame_waiters_) {
    // TODO(fxbug.dev/50018): async::Wait destructor ordering edge case
    it.second->Cancel();
    it.second = nullptr;
  }
  camera_ = nullptr;
}

void VirtualCameraImpl::OnSetBufferCollection(
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  buffers_.reset();
  while (!free_buffers_.empty()) {
    free_buffers_.pop();
  }

  fuchsia::sysmem::BufferCollectionPtr collection;
  allocator_->BindSharedCollection(std::move(token), collection.NewRequest(loop_.dispatcher()));
  collection.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "BufferCollection server disconnected.";
    camera_ = nullptr;
  });
  collection->SetConstraints(true, DefaultBufferConstraints());
  collection->WaitForBuffersAllocated(
      [this, collection = std::move(collection)](zx_status_t status,
                                                 fuchsia::sysmem::BufferCollectionInfo_2 buffers) {
        collection->Close();
        if (status != ZX_OK) {
          FX_PLOGS(ERROR, status) << "Failed to allocate buffers.";
          camera_ = nullptr;
          return;
        }
        buffers_ = std::move(buffers);
        for (uint32_t i = 0; i < buffers_->buffer_count; ++i) {
          free_buffers_.push(i);
        }
      });
}

void VirtualCameraImpl::FrameTick() {
  ++frame_count_;
  const uint64_t frame_time_ns = (1000000ull * DefaultStreamProperties().frame_rate.denominator) /
                                 DefaultStreamProperties().frame_rate.numerator;
  auto deadline = interrupt_start_time_ + zx::usec(frame_count_ * frame_time_ns);
  ZX_ASSERT(async::PostTaskForTime(loop_.dispatcher(),
                                   fit::bind_member(this, &VirtualCameraImpl::FrameTick),
                                   deadline) == ZX_OK);

  if (!buffers_.has_value()) {
    return;
  }

  if (free_buffers_.size() == DefaultBufferConstraints().min_buffer_count_for_camping) {
    return;
  }

  auto buffer_index = free_buffers_.front();
  free_buffers_.pop();

  FillFrame(buffer_index);
  auto timestamp = zx::clock::get_monotonic();

  zx::eventpair fence;
  fuchsia::camera3::FrameInfo info;
  info.frame_counter = frame_count_;
  info.buffer_index = buffer_index;
  info.timestamp = timestamp.get();
  ZX_ASSERT(zx::eventpair::create(0, &fence, &info.release_fence) == ZX_OK);
  EmbedMetadata(info);

  stream_->AddFrame(std::move(info));

  auto waiter =
      std::make_unique<async::Wait>(fence.get(), ZX_EVENTPAIR_PEER_CLOSED, 0,
                                    [this, fence = std::move(fence), buffer_index](
                                        async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                        zx_status_t status, const zx_packet_signal_t* signal) {
                                      free_buffers_.push(buffer_index);
                                      frame_waiters_.erase(buffer_index);
                                    });
  ZX_ASSERT(waiter->Begin(loop_.dispatcher()) == ZX_OK);
  frame_waiters_[buffer_index] = std::move(waiter);
}

void VirtualCameraImpl::FillFrame(uint32_t buffer_index) {
  auto& buffer = buffers_->buffers[buffer_index];

  fzl::VmoMapper mapper;
  const size_t map_size = buffers_->settings.buffer_settings.size_bytes - buffer.vmo_usable_start;
  zx_status_t status =
      mapper.Map(buffer.vmo, buffer.vmo_usable_start, map_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to map buffer.";
    return;
  }

  auto format = DefaultStreamProperties().image_format;
  auto data = reinterpret_cast<char*>(mapper.start());

  // Render the UV plane with time-varying Y.

  // Luma
  const uint8_t y = 128 + 127.5f * sinf(2 * M_PI * (frame_count_ % 240) / 240.0);
  for (uint32_t row = 0; row < format.coded_height; ++row) {
    memset(data, y, format.bytes_per_row);
    data += format.bytes_per_row;
  }

  // Chroma
  const uint32_t chroma_width = format.coded_width / 2;
  const uint32_t chroma_height = format.coded_height / 2;
  for (uint32_t row = 0; row < chroma_height; ++row) {
    for (uint32_t col = 0; col < chroma_width; ++col) {
      const uint8_t u = (col * 256) / chroma_width;
      const uint8_t v = 255 - (row * 256) / chroma_height;
      data[col * 2] = u;
      data[col * 2 + 1] = v;
    }
    data += format.bytes_per_row;
  }

  zx_cache_flush(mapper.start(), mapper.size(), ZX_CACHE_FLUSH_DATA);
  mapper.Unmap();
}

void VirtualCameraImpl::EmbedMetadata(const fuchsia::camera3::FrameInfo& info) {
  auto& buffer = buffers_->buffers[info.buffer_index];

  fzl::VmoMapper mapper;
  auto buffer_size = buffers_->settings.buffer_settings.size_bytes;
  const size_t map_size = buffer_size - buffer.vmo_usable_start;
  zx_status_t status =
      mapper.Map(buffer.vmo, buffer.vmo_usable_start, map_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to map buffer.";
    return;
  }
  auto image_data = reinterpret_cast<char*>(mapper.start());

  image_data += Embed(image_data, kEmbeddedMetadataMagic, buffer_size);
  image_data += Embed(image_data, info.buffer_index, buffer_size);
  image_data += Embed(image_data, info.frame_counter, buffer_size);
  image_data += Embed(image_data, info.timestamp, buffer_size);

  mapper.Unmap();
}

}  // namespace camera
