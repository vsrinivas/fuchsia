// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/test/fakes/fake_image_pipe.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

#include <iomanip>
#include <iostream>

#include "src/media/playback/mediaplayer/test/fakes/formatting.h"

namespace media_player {
namespace test {

FakeImagePipe::FakeImagePipe(fuchsia::sysmem::Allocator* sysmem_allocator)
    : sysmem_allocator_(sysmem_allocator),
      dispatcher_(async_get_default_dispatcher()),
      binding_(this),
      weak_factory_(this) {
  FX_CHECK(sysmem_allocator_);
}

FakeImagePipe::~FakeImagePipe() {
  while (!image_presentation_queue_.empty()) {
    FX_DCHECK(image_presentation_queue_.front().release_fences_);
    for (auto& release_fence : *image_presentation_queue_.front().release_fences_) {
      release_fence.signal(0, ZX_EVENT_SIGNALED);
    }

    image_presentation_queue_.pop_front();
  }
}

void FakeImagePipe::Bind(fidl::InterfaceRequest<fuchsia::images::ImagePipe2> request) {
  binding_.Bind(std::move(request));
}

void FakeImagePipe::OnPresentScene(zx::time presentation_time, zx::time next_presentation_time,
                                   zx::duration presentation_interval) {
  next_presentation_time_ = next_presentation_time;
  presentation_interval_ = presentation_interval;

  while (image_presentation_queue_.size() > 1 &&
         image_presentation_queue_.front().presentation_time_ <
             static_cast<uint64_t>(presentation_time.get())) {
    FX_DCHECK(image_presentation_queue_.front().release_fences_);
    for (auto& release_fence : *(image_presentation_queue_.front().release_fences_)) {
      release_fence.signal(0, ZX_EVENT_SIGNALED);
    }

    image_presentation_queue_.pop_front();
  }
}

void FakeImagePipe::AddBufferCollection(
    uint32_t buffer_collection_id,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> buffer_collection_token) {
  if (buffer_collections_by_id_.find(buffer_collection_id) != buffer_collections_by_id_.end()) {
    FX_LOGS(ERROR) << "AddBufferCollection called for existing collection " << buffer_collection_id;
    expected_ = false;
    binding_.Unbind();
    return;
  }

  FX_CHECK(sysmem_allocator_);
  buffer_collections_by_id_.emplace(
      buffer_collection_id,
      std::make_unique<BufferCollection>(std::move(buffer_collection_token), sysmem_allocator_));
}

void FakeImagePipe::AddImage(uint32_t image_id, uint32_t buffer_collection_id,
                             uint32_t buffer_collection_index,
                             fuchsia::sysmem::ImageFormat_2 image_format) {
  if (dump_expectations_) {
    std::cerr << "// Format for image " << image_id << "\n";
    std::cerr << image_format << "\n";
  }

  if (image_id == expected_black_image_id_) {
    if (expected_black_image_format_) {
      ExpectImageFormat(*expected_black_image_format_, image_format);
    }
  } else if (expected_image_format_) {
    ExpectImageFormat(*expected_image_format_, image_format);
  }

  auto [iter, inserted] = images_by_id_.emplace(
      image_id, Image(std::move(image_format), buffer_collection_id, buffer_collection_index));
  if (!inserted) {
    FX_LOGS(ERROR) << "AddImage image_id: (" << image_id
                   << ") refers to existing image, closing connection.";
    expected_ = false;
    binding_.Unbind();
    return;
  }
}

void FakeImagePipe::RemoveBufferCollection(uint32_t buffer_collection_id) {
  if (buffer_collections_by_id_.erase(buffer_collection_id) != 1) {
    FX_LOGS(ERROR) << "RemoveBufferCollection called for unrecognized id " << buffer_collection_id;
    expected_ = false;
    binding_.Unbind();
    return;
  }

  // Remove images referencing the collection.
  auto iter = images_by_id_.begin();
  while (iter != images_by_id_.end()) {
    if (iter->second.buffer_collection_id_ == buffer_collection_id) {
      uint32_t image_id = iter->first;
      iter = images_by_id_.erase(iter);
      for (auto& image_presentation : image_presentation_queue_) {
        if (image_presentation.image_id_ == image_id) {
          FX_DCHECK(image_presentation.release_fences_);
          for (auto& release_fence : *image_presentation.release_fences_) {
            release_fence.signal(0, ZX_EVENT_SIGNALED);
          }
        }
      }
    } else {
      ++iter;
    }
  }
}

void FakeImagePipe::RemoveImage(uint32_t image_id) {
  auto removed = images_by_id_.erase(image_id);
  if (removed != 1) {
    FX_LOGS(ERROR) << "RemoveImage: image_id (" << image_id
                   << ") not recognized, closing connection.";
    expected_ = false;
    binding_.Unbind();
    return;
  }

  for (auto& image_presentation : image_presentation_queue_) {
    if (image_presentation.image_id_ == image_id) {
      FX_DCHECK(image_presentation.release_fences_);
      for (auto& release_fence : *image_presentation.release_fences_) {
        release_fence.signal(0, ZX_EVENT_SIGNALED);
      }
    }
  }
}

void FakeImagePipe::PresentImage(uint32_t image_id, uint64_t presentation_time,
                                 std::vector<zx::event> acquire_fences,
                                 std::vector<zx::event> release_fences,
                                 PresentImageCallback callback) {
  FX_DCHECK(callback);
  // The video renderer doesn't use the acquire fences, so we don't support
  // them in the fake.
  FX_CHECK(acquire_fences.empty()) << "PresentImage: acquire_fences not supported.";

  if (prev_presentation_time_ > presentation_time) {
    FX_LOGS(ERROR) << "PresentImage: presentation_time (" << presentation_time
                   << ") less than previous (" << prev_presentation_time_
                   << "), closing connection.";
    expected_ = false;
    binding_.Unbind();
  }

  prev_presentation_time_ = presentation_time;

  if (initial_presentation_time_ == 0 && presentation_time != 0) {
    initial_presentation_time_ = presentation_time;
  }

  auto iter = images_by_id_.find(image_id);
  if (iter == images_by_id_.end()) {
    FX_LOGS(ERROR) << "PresentImage: image_id (" << image_id
                   << ") not recognized, closing connection.";
    expected_ = false;
    binding_.Unbind();
    return;
  }

  Image& image = iter->second;

  uint64_t size = image.image_format_.bytes_per_row * image.image_format_.coded_height;

  void* image_payload = GetPayload(image.buffer_collection_id_, image.buffer_index_);

  if (dump_expectations_) {
    // Here's we're dumping the packet to the console to generate some C++ code
    // that can easily be pasted into a test as a 'golden'. We use |std::cerr|,
    // because we don't want/need the usual log line header we get with FX_LOGS.
    // Also, this ends up on the console, not in the logs.
    std::cerr << "{ " << presentation_time - initial_presentation_time_ << ", " << size << ", 0x"
              << std::hex << std::setw(16) << std::setfill('0')
              << PacketHash(image_payload, image.image_format_) << std::dec << " },\n";
  }

  if (!expected_packets_info_.empty()) {
    if (expected_packets_info_iter_ == expected_packets_info_.end()) {
      FX_LOGS(ERROR) << "PresentImage: frame supplied after expected packets";
      expected_ = false;
    }

    if (expected_packets_info_iter_->size() != size ||
        expected_packets_info_iter_->hash() != PacketHash(image_payload, image.image_format_)) {
      FX_LOGS(ERROR) << "PresentImage: supplied frame doesn't match expected packet info";
      FX_LOGS(ERROR) << "actual:   " << presentation_time - initial_presentation_time_ << ", "
                     << size << ", 0x" << std::hex << std::setw(16) << std::setfill('0')
                     << PacketHash(image_payload, image.image_format_) << std::dec;
      FX_LOGS(ERROR) << "expected: " << expected_packets_info_iter_->pts() << ", "
                     << expected_packets_info_iter_->size() << ", 0x" << std::hex << std::setw(16)
                     << std::setfill('0') << expected_packets_info_iter_->hash() << std::dec;
      expected_ = false;
    }

    ++expected_packets_info_iter_;
  }

  image_presentation_queue_.emplace_back(image_id, presentation_time, std::move(release_fences));

  async::PostTask(dispatcher_, [this, callback = std::move(callback), weak_this = GetWeakThis()]() {
    if (!weak_this) {
      callback(
          fuchsia::images::PresentationInfo{.presentation_time = 0, .presentation_interval = 0});
      return;
    }

    callback(fuchsia::images::PresentationInfo{
        .presentation_time = static_cast<uint64_t>(next_presentation_time_.get()),
        .presentation_interval = static_cast<uint64_t>(presentation_interval_.get())});
  });
}

void FakeImagePipe::ExpectImageFormat(const fuchsia::sysmem::ImageFormat_2& expected,
                                      const fuchsia::sysmem::ImageFormat_2& actual) {
  if (actual.pixel_format.type != expected.pixel_format.type) {
    FX_LOGS(ERROR) << "ExpectImageFormat: unexpected ImageFormat.pixel_format.type value "
                   << fidl::ToUnderlying(actual.pixel_format.type);
    expected_ = false;
  }

  if (actual.coded_width != expected.coded_width) {
    FX_LOGS(ERROR) << "ExpectImageFormat: unexpected ImageFormat.coded_width value "
                   << actual.coded_width;
    expected_ = false;
  }

  if (actual.coded_height != expected.coded_height) {
    FX_LOGS(ERROR) << "ExpectImageFormat: unexpected ImageFormat.coded_height value "
                   << actual.coded_height;
    expected_ = false;
  }

  if (actual.bytes_per_row != expected.bytes_per_row) {
    FX_LOGS(ERROR) << "ExpectImageFormat: unexpected ImageFormat.bytes_per_row value "
                   << actual.bytes_per_row;
    expected_ = false;
  }

  if (actual.display_width != expected.display_width) {
    FX_LOGS(ERROR) << "ExpectImageFormat: unexpected ImageFormat.display_width value "
                   << actual.display_width;
    expected_ = false;
  }

  if (actual.display_height != expected.display_height) {
    FX_LOGS(ERROR) << "ExpectImageFormat: unexpected ImageFormat.display_height value "
                   << actual.display_height;
    expected_ = false;
  }

  if (actual.color_space.type != expected.color_space.type) {
    FX_LOGS(ERROR) << "ExpectImageFormat: unexpected ImageFormat.color_space.type value "
                   << fidl::ToUnderlying(actual.color_space.type);
    expected_ = false;
  }

  if (actual.has_pixel_aspect_ratio != expected.has_pixel_aspect_ratio) {
    FX_LOGS(ERROR) << "ExpectImageFormat: unexpected ImageFormat.has_pixel_aspect_ratio value "
                   << actual.has_pixel_aspect_ratio;
    expected_ = false;
  }

  if (actual.pixel_aspect_ratio_width != expected.pixel_aspect_ratio_width) {
    FX_LOGS(ERROR) << "ExpectImageFormat: unexpected ImageFormat.pixel_aspect_ratio_width value "
                   << actual.pixel_aspect_ratio_width;
    expected_ = false;
  }

  if (actual.pixel_aspect_ratio_height != expected.pixel_aspect_ratio_height) {
    FX_LOGS(ERROR) << "ExpectImageFormat: unexpected ImageFormat.pixel_aspect_ratio_height value "
                   << actual.pixel_aspect_ratio_height;
    expected_ = false;
  }
}

uint64_t FakeImagePipe::PacketHash(const void* data,
                                   const fuchsia::sysmem::ImageFormat_2& image_format) {
  FX_DCHECK(data);
  FX_DCHECK(image_format.pixel_format.type == fuchsia::sysmem::PixelFormatType::I420);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
  uint64_t hash = 0;

  // Hash the Y plane.
  for (uint32_t line = 0; line < image_format.display_height; ++line) {
    hash = PacketInfo::Hash(bytes, image_format.display_width, hash);
    bytes += image_format.bytes_per_row;
  }

  bytes += image_format.bytes_per_row * (image_format.coded_height - image_format.display_height);

  // Hash the V plane.
  for (uint32_t line = 0; line < image_format.display_height / 2; ++line) {
    hash = PacketInfo::Hash(bytes, image_format.display_width / 2, hash);
    bytes += image_format.bytes_per_row / 2;
  }

  bytes +=
      (image_format.bytes_per_row * (image_format.coded_height - image_format.display_height)) / 4;

  // Hash the U plane.
  for (uint32_t line = 0; line < image_format.display_height / 2; ++line) {
    hash = PacketInfo::Hash(bytes, image_format.display_width / 2, hash);
    bytes += image_format.bytes_per_row / 2;
  }

  return hash;
}

void* FakeImagePipe::GetPayload(uint32_t buffer_collection_id, uint32_t buffer_index) {
  auto iter = buffer_collections_by_id_.find(buffer_collection_id);
  if (iter == buffer_collections_by_id_.end()) {
    FX_LOGS(ERROR) << "GetPayload: unrecognized buffer collection id " << buffer_collection_id;
    expected_ = false;
    return nullptr;
  }

  auto& collection = iter->second;
  FX_CHECK(collection);
  if (!collection->ready_) {
    FX_LOGS(ERROR) << "GetPayload: buffer collection " << buffer_collection_id << " not ready";
    expected_ = false;
    return nullptr;
  }

  if (buffer_index >= collection->buffers_.size()) {
    FX_LOGS(ERROR) << "GetPayload: buffer index " << buffer_index << " out of range for collection "
                   << buffer_collection_id << " of size " << collection->buffers_.size();
    expected_ = false;
    return nullptr;
  }

  return collection->buffers_[buffer_index].start();
}

////////////////////////////////////////////////////////////////////////////////
// FakeImagePipe::BufferCollection implementation.

FakeImagePipe::BufferCollection::BufferCollection(
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_handle,
    fuchsia::sysmem::Allocator* sysmem_allocator) {
  FX_CHECK(token_handle.is_valid());
  FX_CHECK(sysmem_allocator);
  token_ = token_handle.Bind();
  FX_CHECK(token_);
  token_->Sync([this, sysmem_allocator]() {
    auto handle = token_.Unbind();
    sysmem_allocator->BindSharedCollection(std::move(handle), collection_.NewRequest());

    fuchsia::sysmem::BufferCollectionConstraints constraints{
        .usage = fuchsia::sysmem::BufferUsage{.cpu = fuchsia::sysmem::cpuUsageRead |
                                                     fuchsia::sysmem::cpuUsageReadOften},
        .min_buffer_count_for_camping = 0,
        .min_buffer_count_for_dedicated_slack = 0,
        .min_buffer_count_for_shared_slack = 0,
        .min_buffer_count = 0,
        .max_buffer_count = 0,
        .has_buffer_memory_constraints = true,
        .image_format_constraints_count = 0};
    constraints.buffer_memory_constraints.heap_permitted_count = 0;
    constraints.buffer_memory_constraints.ram_domain_supported = true;

    collection_->SetConstraints(true, constraints);

    collection_->WaitForBuffersAllocated(
        [this](zx_status_t status, fuchsia::sysmem::BufferCollectionInfo_2 collection_info) {
          if (status != ZX_OK) {
            FX_PLOGS(ERROR, status) << "Sysmem buffer allocation failed";
            return;
          }

          buffers_.resize(collection_info.buffer_count);

          for (uint32_t i = 0; i < collection_info.buffer_count; ++i) {
            auto& buffer_info = collection_info.buffers[i];
            FX_DCHECK(buffer_info.vmo_usable_start == 0);
            FX_DCHECK(buffer_info.vmo);

            uint64_t size;
            zx_status_t status = buffer_info.vmo.get_size(&size);
            if (status != ZX_OK) {
              FX_PLOGS(ERROR, status) << "Couldn't get vmo size";
              return;
            }

            auto& buffer = buffers_[i];
            status = buffer.Map(buffer_info.vmo, 0, size, ZX_VM_PERM_READ);
            if (status != ZX_OK) {
              FX_PLOGS(ERROR, status) << "Couldn't map vmo";
              return;
            }
          }

          ready_ = true;
        });
  });
}

////////////////////////////////////////////////////////////////////////////////
// FakeImagePipe::Image implementation.

FakeImagePipe::Image::Image(fuchsia::sysmem::ImageFormat_2 image_format,
                            uint32_t buffer_collection_id, uint32_t buffer_index)
    : image_format_(std::move(image_format)),
      buffer_collection_id_(buffer_collection_id),
      buffer_index_(buffer_index) {}

}  // namespace test
}  // namespace media_player
