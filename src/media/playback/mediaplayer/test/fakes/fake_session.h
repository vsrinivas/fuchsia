// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_SESSION_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_SESSION_H_

#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/optional.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/media/playback/mediaplayer/test/fakes/fake_image_pipe.h"
#include "src/media/playback/mediaplayer/test/fakes/packet_info.h"

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

  // Indicates that the session should print out expected frame info.
  void DumpExpectations(uint32_t display_height);

  // Indicates that the session should verify supplied frames against the
  // specified PacketInfos.
  void SetExpectations(uint32_t black_image_id,
                       const fuchsia::images::ImageInfo& black_image_info,
                       const fuchsia::images::ImageInfo& info,
                       uint32_t display_height,
                       const std::vector<PacketInfo>&& expected_packets_info);

  // Returns true if everything has gone as expected so far.
  bool expected() {
    return expected_ && (!image_pipe_ || image_pipe_->expected());
  }

  // Session implementation.
  void Enqueue(std::vector<::fuchsia::ui::scenic::Command> cmds) override;

  void Present(uint64_t presentation_time,
               std::vector<::zx::event> acquire_fences,
               std::vector<::zx::event> release_fences,
               PresentCallback callback) override;

  void HitTest(uint32_t node_id, ::fuchsia::ui::gfx::vec3 ray_origin,
               ::fuchsia::ui::gfx::vec3 ray_direction,
               HitTestCallback callback) override;

  void HitTestDeviceRay(::fuchsia::ui::gfx::vec3 ray_origin,
                        ::fuchsia::ui::gfx::vec3 ray_direction,
                        HitTestDeviceRayCallback callback) override;

  void SetDebugName(std::string debug_name) override {}

 private:
  static constexpr uint32_t kNullResourceId = 0;

  struct Resource {
    Resource(fuchsia::ui::gfx::ResourceArgs args) : args_(std::move(args)) {}

    fuchsia::ui::gfx::ResourceArgs args_;
    uint32_t parent_ = kNullResourceId;
    std::unordered_set<uint32_t> children_;
    uint32_t material_ = kNullResourceId;
    uint32_t texture_ = kNullResourceId;
  };

  // Gets a weak pointer to this |FakeSession|.
  fxl::WeakPtr<FakeSession> GetWeakThis() { return weak_factory_.GetWeakPtr(); }

  Resource* FindResource(uint32_t id);

  fuchsia::ui::gfx::ImagePipeArgs* FindVideoImagePipe(uint32_t node_id);

  void HandleCreateResource(uint32_t resource_id,
                            fuchsia::ui::gfx::ResourceArgs args);

  void HandleReleaseResource(uint32_t resource_id);

  void HandleAddChild(uint32_t parent_id, uint32_t child_id);

  void HandleSetMaterial(uint32_t node_id, uint32_t material_id);

  void HandleSetTexture(uint32_t material_id, uint32_t texture_id);

  // Fake-presents a scene and schedules the next scene presentation.
  void PresentScene();

  async_dispatcher_t* dispatcher_;
  fidl::Binding<::fuchsia::ui::scenic::Session> binding_;
  ::fuchsia::ui::scenic::SessionListenerPtr listener_;

  std::unordered_map<uint32_t, Resource> resources_by_id_;

  std::unique_ptr<FakeImagePipe> image_pipe_;

  bool dump_expectations_ = false;
  uint32_t expected_black_image_id_ = 0;
  std::unique_ptr<fuchsia::images::ImageInfo> expected_black_image_info_;
  std::unique_ptr<fuchsia::images::ImageInfo> expected_image_info_;
  std::vector<PacketInfo> expected_packets_info_;
  uint32_t expected_display_height_ = 0;
  bool expected_ = true;

  fxl::WeakPtrFactory<FakeSession> weak_factory_;
  zx::time next_presentation_time_;
};

}  // namespace test
}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_SESSION_H_
