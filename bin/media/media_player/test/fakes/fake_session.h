// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKE_SESSION_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKE_SESSION_H_

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include "garnet/bin/media/media_player/test/fakes/packet_info.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/media/transport/mapped_shared_buffer.h"

namespace media_player {
namespace test {

// Implements ViewManager for testing.
class FakeSession : public ::fuchsia::ui::scenic::Session {
 public:
  FakeSession();

  ~FakeSession() override;

  // Binds the session.
  void Bind(fidl::InterfaceRequest<::fuchsia::ui::scenic::Session> request,
            ::fuchsia::ui::scenic::SessionListenerPtr listener);

  // Indicates that the session should print out supplied packet info.
  void DumpPackets() { dump_packets_ = true; }

  // Indicates that the session should verify supplied frames against the
  // specified PacketInfos.
  void ExpectPackets(const std::vector<PacketInfo>&& expected_packets_info) {
    expected_packets_info_ = std::move(expected_packets_info);
    expected_packets_info_iter_ = expected_packets_info_.begin();
  }

  // Indicates the the session should verify supplied ImageInfos against |info|.
  void ExpectImageInfo(const fuchsia::images::ImageInfo& info) {
    expected_image_info_ = fidl::MakeOptional(info);
  }

  // Returns true if everything has gone as expected so far.
  bool expected() { return expected_; }

  // Session implementation.
  void Enqueue(fidl::VectorPtr<::fuchsia::ui::scenic::Command> cmds) override;

  void Present(uint64_t presentation_time,
               fidl::VectorPtr<::zx::event> acquire_fences,
               fidl::VectorPtr<::zx::event> release_fences,
               PresentCallback callback) override;

  void HitTest(uint32_t node_id, ::fuchsia::ui::gfx::vec3 ray_origin,
               ::fuchsia::ui::gfx::vec3 ray_direction,
               HitTestCallback callback) override;

  void HitTestDeviceRay(::fuchsia::ui::gfx::vec3 ray_origin,
                        ::fuchsia::ui::gfx::vec3 ray_direction,
                        HitTestDeviceRayCallback callback) override;

 private:
  static constexpr uint32_t kNullResourceId = 0;

  struct Resource {
    Resource(fuchsia::ui::gfx::ResourceArgs args) : args_(std::move(args)) {}

    fuchsia::ui::gfx::ResourceArgs args_;
    uint32_t parent_ = kNullResourceId;
    std::unordered_set<uint32_t> children_;
    uint32_t material_ = kNullResourceId;

    // For materials only.
    std::unique_ptr<fuchsia::ui::gfx::ImageArgs> image_texture_;

    // For memory only.
    media::MappedSharedBuffer mapped_buffer_;
  };

  Resource* FindResource(uint32_t id);

  fuchsia::ui::gfx::ImageArgs* FindVideoImage(uint32_t node_id);

  void HandleCreateResource(uint32_t resource_id,
                            fuchsia::ui::gfx::ResourceArgs args);

  void HandleReleaseResource(uint32_t resource_id);

  void HandleAddChild(uint32_t parent_id, uint32_t child_id);

  void HandleSetMaterial(uint32_t node_id, uint32_t material_id);

  void HandleSetTexture(uint32_t material_id, uint32_t texture_id);

  async_t* async_;
  fidl::Binding<::fuchsia::ui::scenic::Session> binding_;
  ::fuchsia::ui::scenic::SessionListenerPtr listener_;

  std::unordered_map<uint32_t, Resource> resources_by_id_;

  bool dump_packets_ = false;
  std::vector<PacketInfo> expected_packets_info_;
  std::vector<PacketInfo>::iterator expected_packets_info_iter_;
  std::unique_ptr<fuchsia::images::ImageInfo> expected_image_info_;
  bool expected_ = true;
  uint64_t initial_presentation_time_ = 0;
};

}  // namespace test
}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKE_SESSION_H_
