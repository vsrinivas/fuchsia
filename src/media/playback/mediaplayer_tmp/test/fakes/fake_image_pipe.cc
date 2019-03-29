// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer_tmp/test/fakes/fake_image_pipe.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <iomanip>
#include <iostream>
#include "src/lib/fxl/logging.h"

namespace media_player {
namespace test {

FakeImagePipe::FakeImagePipe()
    : dispatcher_(async_get_default_dispatcher()),
      binding_(this),
      weak_factory_(this) {}

FakeImagePipe::~FakeImagePipe() {
  while (!image_presentation_queue_.empty()) {
    FXL_DCHECK(image_presentation_queue_.front().release_fences_);
    for (auto& release_fence :
         *image_presentation_queue_.front().release_fences_) {
      release_fence.signal(0, ZX_EVENT_SIGNALED);
    }

    image_presentation_queue_.pop_front();
  }
}

void FakeImagePipe::Bind(
    fidl::InterfaceRequest<fuchsia::images::ImagePipe> request) {
  binding_.Bind(std::move(request));
}

void FakeImagePipe::OnPresentScene(zx::time presentation_time,
                                   zx::time next_presentation_time,
                                   zx::duration presentation_interval) {
  next_presentation_time_ = next_presentation_time;
  presentation_interval_ = presentation_interval;

  while (image_presentation_queue_.size() > 1 &&
         image_presentation_queue_.front().presentation_time_ <
             static_cast<uint64_t>(presentation_time.get())) {
    FXL_DCHECK(image_presentation_queue_.front().release_fences_);
    for (auto& release_fence :
         *(image_presentation_queue_.front().release_fences_)) {
      release_fence.signal(0, ZX_EVENT_SIGNALED);
    }

    image_presentation_queue_.pop_front();
  }
}

void FakeImagePipe::AddImage(uint32_t image_id,
                             fuchsia::images::ImageInfo image_info,
                             zx::vmo memory, uint64_t offset_bytes,
                             uint64_t size_bytes,
                             fuchsia::images::MemoryType memory_type) {
  if (image_id == expected_black_image_id_) {
    if (expected_black_image_info_) {
      ExpectImageInfo(*expected_black_image_info_, image_info);
    }
  } else {
    if (dump_expectations_) {
      FXL_DCHECK(image_info.pixel_format == fuchsia::images::PixelFormat::YV12);
      std::cerr << "{.width = " << image_info.width << ",\n"
                << ".height = " << image_info.height << ",\n"
                << ".stride = " << image_info.stride << ",\n"
                << ".pixel_format = fuchsia::images::PixelFormat::YV12,\n"
                << "};\n";
    }

    if (expected_image_info_) {
      ExpectImageInfo(*expected_image_info_, image_info);
    }
  }

  auto [iter, inserted] = images_by_id_.emplace(
      image_id, Image(std::move(image_info), std::move(memory), offset_bytes,
                      size_bytes));
  if (!inserted) {
    FXL_LOG(ERROR) << "AddImage image_id: (" << image_id
                   << ") refers to existing image, closing connection.";
    expected_ = false;
    binding_.Unbind();
    return;
  }

  auto& image = iter->second;
  if (image.offset_bytes_ + image.size_bytes_ > image.vmo_mapper_.size()) {
    FXL_LOG(ERROR) << "AddImage image_id: (" << image_id << ") offset_bytes ("
                   << image.offset_bytes_ << ") plus size_bytes ("
                   << image.size_bytes_ << ") exceeds vmo size ("
                   << image.vmo_mapper_.size() << "), closing connection.";
    expected_ = false;
    binding_.Unbind();
    return;
  }
}

void FakeImagePipe::RemoveImage(uint32_t image_id) {
  auto removed = images_by_id_.erase(image_id);
  if (removed != 1) {
    FXL_LOG(ERROR) << "RemoveImage: image_id (" << image_id
                   << ") not recognized, closing connection.";
    expected_ = false;
    binding_.Unbind();
    return;
  }

  for (auto& image_presentation : image_presentation_queue_) {
    if (image_presentation.image_id_ == image_id) {
      FXL_DCHECK(image_presentation.release_fences_);
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
  FXL_DCHECK(callback);
  // The video renderer doesn't use the acquire fences, so we don't support
  // them in the fake.
  FXL_CHECK(acquire_fences.empty())
      << "PresentImage: acquire_fences not supported.";

  if (prev_presentation_time_ > presentation_time) {
    FXL_LOG(ERROR) << "PresentImage: presentation_time (" << presentation_time
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
    FXL_LOG(ERROR) << "PresentImage: image_id (" << image_id
                   << ") not recognized, closing connection.";
    expected_ = false;
    binding_.Unbind();
    return;
  }

  Image& image = iter->second;

  uint64_t size = image.image_info_.stride * image.image_info_.height;

  if (image.offset_bytes_ + size > image.vmo_mapper_.size()) {
    FXL_LOG(ERROR) << "PresentImage: image exceeds vmo limits";
    FXL_LOG(ERROR) << "    vmo size     " << image.vmo_mapper_.size();
    FXL_LOG(ERROR) << "    image offset " << image.offset_bytes_;
    FXL_LOG(ERROR) << "    image stride " << image.image_info_.stride;
    FXL_LOG(ERROR) << "    image height " << image.image_info_.height;
    expected_ = false;
    return;
  }

  void* image_payload = reinterpret_cast<uint8_t*>(image.vmo_mapper_.start()) +
                        image.offset_bytes_;

  if (dump_expectations_) {
    // Here's we're dumping the packet to the console to generate some C++ code
    // that can easily be pasted into a test as a 'golden'. We use |std::cerr|,
    // because we don't want/need the usual log line header we get with FXL_LOG.
    // Also, this ends up on the console, not in the logs.
    std::cerr << "{ " << presentation_time - initial_presentation_time_ << ", "
              << size << ", 0x" << std::hex << std::setw(16)
              << std::setfill('0')
              << PacketHash(image_payload, image.image_info_) << std::dec
              << " },\n";
  }

  if (!expected_packets_info_.empty()) {
    if (expected_packets_info_iter_ == expected_packets_info_.end()) {
      FXL_LOG(ERROR) << "PresentImage: frame supplied after expected packets";
      expected_ = false;
    }

    if (expected_packets_info_iter_->size() != size ||
        expected_packets_info_iter_->hash() !=
            PacketHash(image_payload, image.image_info_)) {
      FXL_LOG(ERROR)
          << "PresentImage: supplied frame doesn't match expected packet info";
      FXL_LOG(ERROR) << "actual:   "
                     << presentation_time - initial_presentation_time_ << ", "
                     << size << ", 0x" << std::hex << std::setw(16)
                     << std::setfill('0')
                     << PacketHash(image_payload, image.image_info_)
                     << std::dec;
      FXL_LOG(ERROR) << "expected: " << expected_packets_info_iter_->pts()
                     << ", " << expected_packets_info_iter_->size() << ", 0x"
                     << std::hex << std::setw(16) << std::setfill('0')
                     << expected_packets_info_iter_->hash() << std::dec;
      expected_ = false;
    }

    ++expected_packets_info_iter_;
  }

  image_presentation_queue_.emplace_back(image_id, presentation_time,
                                         std::move(release_fences));

  async::PostTask(dispatcher_, [this, callback = std::move(callback),
                                weak_this = GetWeakThis()]() {
    if (!weak_this) {
      callback(fuchsia::images::PresentationInfo{.presentation_time = 0,
                                                 .presentation_interval = 0});
      return;
    }

    callback(fuchsia::images::PresentationInfo{
        .presentation_time =
            static_cast<uint64_t>(next_presentation_time_.get()),
        .presentation_interval =
            static_cast<uint64_t>(presentation_interval_.get())});
  });
}

void FakeImagePipe::ExpectImageInfo(const fuchsia::images::ImageInfo& expected,
                                    const fuchsia::images::ImageInfo& actual) {
  if (actual.transform != expected.transform) {
    FXL_LOG(ERROR) << "ExpectImageInfo: unexpected ImageInfo.transform value "
                   << fidl::ToUnderlying(actual.transform);
    expected_ = false;
  }

  if (actual.width != expected.width) {
    FXL_LOG(ERROR) << "ExpectImageInfo: unexpected ImageInfo.width value "
                   << actual.width;
    expected_ = false;
  }

  if (actual.height != expected.height) {
    FXL_LOG(ERROR) << "ExpectImageInfo: unexpected ImageInfo.height value "
                   << actual.height;
    expected_ = false;
  }

  if (actual.stride != expected.stride) {
    FXL_LOG(ERROR) << "ExpectImageInfo: unexpected ImageInfo.stride value "
                   << actual.stride;
    expected_ = false;
  }

  if (actual.pixel_format != expected.pixel_format) {
    FXL_LOG(ERROR)
        << "ExpectImageInfo: unexpected ImageInfo.pixel_format value "
        << fidl::ToUnderlying(actual.pixel_format);
    expected_ = false;
  }

  if (actual.color_space != expected.color_space) {
    FXL_LOG(ERROR) << "ExpectImageInfo: unexpected ImageInfo.color_space value "
                   << fidl::ToUnderlying(actual.color_space);
    expected_ = false;
  }

  if (actual.tiling != expected.tiling) {
    FXL_LOG(ERROR) << "ExpectImageInfo: unexpected ImageInfo.tiling value "
                   << fidl::ToUnderlying(actual.tiling);
    expected_ = false;
  }

  if (actual.alpha_format != expected.alpha_format) {
    FXL_LOG(ERROR)
        << "ExpectImageInfo: unexpected ImageInfo.alpha_format value "
        << fidl::ToUnderlying(actual.alpha_format);
    expected_ = false;
  }
}

uint64_t FakeImagePipe::PacketHash(
    const void* data, const fuchsia::images::ImageInfo& image_info) {
  FXL_DCHECK(data);
  FXL_DCHECK(expected_display_height_ <= image_info.height);
  FXL_DCHECK(image_info.pixel_format == fuchsia::images::PixelFormat::YV12);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
  uint64_t hash = 0;

  // Hash the Y plane.
  for (uint32_t line = 0; line < expected_display_height_; ++line) {
    hash = PacketInfo::Hash(bytes, image_info.width, hash);
    bytes += image_info.stride;
  }

  bytes += image_info.stride * (image_info.height - expected_display_height_);

  // Hash the V plane.
  for (uint32_t line = 0; line < expected_display_height_ / 2; ++line) {
    hash = PacketInfo::Hash(bytes, image_info.width / 2, hash);
    bytes += image_info.stride / 2;
  }

  bytes +=
      (image_info.stride * (image_info.height - expected_display_height_)) / 4;

  // Hash the U plane.
  for (uint32_t line = 0; line < expected_display_height_ / 2; ++line) {
    hash = PacketInfo::Hash(bytes, image_info.width / 2, hash);
    bytes += image_info.stride / 2;
  }

  return hash;
}

////////////////////////////////////////////////////////////////////////////////
// FakeImagePipe::Image implementation.

FakeImagePipe::Image::Image(fuchsia::images::ImageInfo image_info,
                            zx::vmo memory, uint64_t offset_bytes,
                            uint64_t size_bytes)
    : image_info_(std::move(image_info)),
      offset_bytes_(offset_bytes),
      size_bytes_(size_bytes) {
  uint64_t size;
  zx_status_t status = memory.get_size(&size);
  FXL_CHECK(status == ZX_OK);

  status = vmo_mapper_.Map(memory, 0, size, ZX_VM_PERM_READ, nullptr);
  FXL_CHECK(status == ZX_OK);
}

}  // namespace test
}  // namespace media_player
