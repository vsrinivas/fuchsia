// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_IMAGE_PIPE_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_IMAGE_PIPE_H_

#include <fuchsia/images/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fzl/vmo-mapper.h>

#include <deque>
#include <unordered_map>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/optional.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/media/playback/mediaplayer/test/fakes/packet_info.h"

namespace media_player {
namespace test {

// Implements ImagePipe for testing.
class FakeImagePipe : public fuchsia::images::ImagePipe {
 public:
  FakeImagePipe();

  ~FakeImagePipe() override;

  // Binds this image pipe.
  void Bind(fidl::InterfaceRequest<fuchsia::images::ImagePipe> request);

  // Indicates that the session should print out expected image info.
  void DumpExpectations(uint32_t display_height) {
    dump_expectations_ = true;
    expected_display_height_ = display_height;
  }

  // Indicates that the session should verify supplied frames against the
  // specified PacketInfos.
  void SetExpectations(uint32_t black_image_id,
                       const fuchsia::images::ImageInfo& black_image_info,
                       const fuchsia::images::ImageInfo& info,
                       uint32_t display_height,
                       const std::vector<PacketInfo>&& expected_packets_info) {
    expected_black_image_id_ = black_image_id;
    expected_black_image_info_ = fidl::MakeOptional(black_image_info);
    expected_image_info_ = fidl::MakeOptional(info);
    expected_display_height_ = display_height;
    expected_packets_info_ = std::move(expected_packets_info);
    expected_packets_info_iter_ = expected_packets_info_.begin();
  }

  // Returns true if everything has gone as expected so far.
  bool expected() { return expected_; }

  // Handles scene presentation.
  void OnPresentScene(zx::time presentation_time,
                      zx::time next_presentation_time,
                      zx::duration presentation_interval);

  // ImagePipe implementation.
  void AddImage(uint32_t image_id, fuchsia::images::ImageInfo image_info,
                zx::vmo memory, uint64_t offset_bytes, uint64_t size_bytes,
                fuchsia::images::MemoryType memory_type) override;

  void RemoveImage(uint32_t image_id) override;

  void PresentImage(uint32_t image_id, uint64_t presentation_time,
                    std::vector<zx::event> acquire_fences,
                    std::vector<zx::event> release_fences,
                    PresentImageCallback callback) override;

 private:
  struct Image {
    Image(fuchsia::images::ImageInfo image_info, zx::vmo memory,
          uint64_t memory_offset, uint64_t size_bytes);

    fuchsia::images::ImageInfo image_info_;
    fzl::VmoMapper vmo_mapper_;
    uint64_t offset_bytes_;
    uint64_t size_bytes_;
  };

  struct ImagePresentation {
    ImagePresentation(uint32_t image_id, uint64_t presentation_time,
                      std::vector<zx::event> release_fences)
        : image_id_(image_id),
          presentation_time_(presentation_time),
          release_fences_(std::move(release_fences)) {}

    uint32_t image_id_;
    uint64_t presentation_time_;
    fidl::VectorPtr<zx::event> release_fences_;
  };

  // Gets a weak pointer to this |FakeImagePipe|.
  fxl::WeakPtr<FakeImagePipe> GetWeakThis() {
    return weak_factory_.GetWeakPtr();
  }

  void ExpectImageInfo(const fuchsia::images::ImageInfo& expected,
                       const fuchsia::images::ImageInfo& actual);

  uint64_t PacketHash(const void* data,
                      const fuchsia::images::ImageInfo& image_info);

  async_dispatcher_t* dispatcher_;
  fidl::Binding<fuchsia::images::ImagePipe> binding_;
  std::unordered_map<uint32_t, Image> images_by_id_;
  std::deque<ImagePresentation> image_presentation_queue_;
  zx::time next_presentation_time_;
  zx::duration presentation_interval_;
  fxl::WeakPtrFactory<FakeImagePipe> weak_factory_;

  bool dump_expectations_ = false;
  std::vector<PacketInfo> expected_packets_info_;
  std::vector<PacketInfo>::iterator expected_packets_info_iter_;
  uint32_t expected_black_image_id_ = 0;
  std::unique_ptr<fuchsia::images::ImageInfo> expected_black_image_info_;
  std::unique_ptr<fuchsia::images::ImageInfo> expected_image_info_;
  uint32_t expected_display_height_ = 720;
  bool expected_ = true;
  uint64_t initial_presentation_time_ = 0;
  uint64_t prev_presentation_time_ = 0;
};

}  // namespace test
}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_IMAGE_PIPE_H_
