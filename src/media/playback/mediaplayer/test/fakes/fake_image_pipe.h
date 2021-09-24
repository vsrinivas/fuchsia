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
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/media/playback/mediaplayer/test/fakes/packet_info.h"

namespace media_player {
namespace test {

// Implements ImagePipe for testing.
class FakeImagePipe : public fuchsia::images::ImagePipe2 {
 public:
  FakeImagePipe(fuchsia::sysmem::Allocator* sysmem_allocator);

  ~FakeImagePipe() override;

  // Binds this image pipe.
  void Bind(fidl::InterfaceRequest<fuchsia::images::ImagePipe2> request);

  // Indicates that the session should print out expected image format.
  void DumpExpectations() { dump_expectations_ = true; }

  // Indicates that the session should verify supplied frames against the
  // specified PacketInfos.
  void SetExpectations(uint32_t black_image_id,
                       const fuchsia::sysmem::ImageFormat_2& black_image_format,
                       const fuchsia::sysmem::ImageFormat_2& format,
                       const std::vector<PacketInfo>&& expected_packets_info) {
    expected_black_image_id_ = black_image_id;
    expected_black_image_format_ =
        std::make_unique<fuchsia::sysmem::ImageFormat_2>(black_image_format);
    expected_image_format_ = std::make_unique<fuchsia::sysmem::ImageFormat_2>(format);
    expected_packets_info_ = std::move(expected_packets_info);
    expected_packets_info_iter_ = expected_packets_info_.begin();
  }

  // Returns true if everything has gone as expected so far.
  bool expected() { return expected_; }

  // Handles scene presentation.
  void OnPresentScene(zx::time presentation_time, zx::time next_presentation_time,
                      zx::duration presentation_interval);

  // ImagePipe2 implementation.
  void AddBufferCollection(uint32_t buffer_collection_id,
                           fidl::InterfaceHandle<class fuchsia::sysmem::BufferCollectionToken>
                               buffer_collection_token) override;

  void AddImage(uint32_t image_id, uint32_t buffer_collection_id, uint32_t buffer_collection_index,
                fuchsia::sysmem::ImageFormat_2 image_format) override;

  void RemoveBufferCollection(uint32_t buffer_collection_id) override;

  void RemoveImage(uint32_t image_id) override;

  void PresentImage(uint32_t image_id, uint64_t presentation_time,
                    std::vector<zx::event> acquire_fences, std::vector<zx::event> release_fences,
                    PresentImageCallback callback) override;

 private:
  struct BufferCollection {
    BufferCollection(fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_handle,
                     fuchsia::sysmem::Allocator* sysmem_allocator);

    fuchsia::sysmem::BufferCollectionTokenPtr token_;
    fuchsia::sysmem::BufferCollectionPtr collection_;
    std::vector<fzl::VmoMapper> buffers_;
    bool ready_ = false;

    // Disallow copy, assign and move.
    BufferCollection(const BufferCollection&) = delete;
    BufferCollection(BufferCollection&&) = delete;
    BufferCollection& operator=(const BufferCollection&) = delete;
    BufferCollection& operator=(BufferCollection&&) = delete;
  };

  struct Image {
    Image(fuchsia::sysmem::ImageFormat_2 image_format, uint32_t buffer_collection_id,
          uint32_t buffer_index);

    fuchsia::sysmem::ImageFormat_2 image_format_;
    uint32_t buffer_collection_id_;
    uint32_t buffer_index_;
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
  fxl::WeakPtr<FakeImagePipe> GetWeakThis() { return weak_factory_.GetWeakPtr(); }

  void ExpectImageFormat(const fuchsia::sysmem::ImageFormat_2& expected,
                         const fuchsia::sysmem::ImageFormat_2& actual);

  uint64_t PacketHash(const void* data, const fuchsia::sysmem::ImageFormat_2& image_format);

  void* GetPayload(uint32_t buffer_collection_id, uint32_t buffer_index);

  fuchsia::sysmem::Allocator* sysmem_allocator_;
  async_dispatcher_t* dispatcher_;
  fidl::Binding<fuchsia::images::ImagePipe2> binding_;
  std::unordered_map<uint32_t, Image> images_by_id_;
  std::unordered_map<uint32_t, std::unique_ptr<BufferCollection>> buffer_collections_by_id_;
  std::deque<ImagePresentation> image_presentation_queue_;
  zx::time next_presentation_time_;
  zx::duration presentation_interval_;
  fxl::WeakPtrFactory<FakeImagePipe> weak_factory_;

  bool dump_expectations_ = false;
  std::vector<PacketInfo> expected_packets_info_;
  std::vector<PacketInfo>::iterator expected_packets_info_iter_;
  uint32_t expected_black_image_id_ = 0;
  std::unique_ptr<fuchsia::sysmem::ImageFormat_2> expected_black_image_format_;
  std::unique_ptr<fuchsia::sysmem::ImageFormat_2> expected_image_format_;
  bool expected_ = true;
  uint64_t initial_presentation_time_ = 0;
  uint64_t prev_presentation_time_ = 0;

  // Disallow copy, assign and move.
  FakeImagePipe(const FakeImagePipe&) = delete;
  FakeImagePipe(FakeImagePipe&&) = delete;
  FakeImagePipe& operator=(const FakeImagePipe&) = delete;
  FakeImagePipe& operator=(FakeImagePipe&&) = delete;
};

}  // namespace test
}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_IMAGE_PIPE_H_
