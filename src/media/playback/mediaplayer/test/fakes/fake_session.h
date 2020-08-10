// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_SESSION_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_SESSION_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
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

// Implements Scenic for testing.
class FakeSession : public fuchsia::ui::scenic::Session {
 public:
  FakeSession();

  ~FakeSession() override;

  void SetSysmemAllocator(fuchsia::sysmem::Allocator* sysmem_allocator) {
    sysmem_allocator_ = sysmem_allocator;
  }

  // Binds the session.
  void Bind(fidl::InterfaceRequest<fuchsia::ui::scenic::Session> request,
            fuchsia::ui::scenic::SessionListenerPtr listener);

  // Indicates that the session should print out expected frame info.
  void DumpExpectations();

  // Indicates that the session should verify supplied frames against the
  // specified PacketInfos.
  void SetExpectations(uint32_t black_image_id,
                       const fuchsia::sysmem::ImageFormat_2& black_image_format,
                       const fuchsia::sysmem::ImageFormat_2& format,
                       const std::vector<PacketInfo>&& expected_packets_info);

  // Returns true if everything has gone as expected so far.
  bool expected() {
    DetectZFighting();
    return expected_ && (!image_pipe_ || image_pipe_->expected());
  }

  // Session implementation.
  void Enqueue(std::vector<fuchsia::ui::scenic::Command> cmds) override;

  void Present(uint64_t presentation_time, std::vector<zx::event> acquire_fences,
               std::vector<zx::event> release_fences, PresentCallback callback) override;

  void Present2(fuchsia::ui::scenic::Present2Args args, Present2Callback callback) override;

  void RequestPresentationTimes(zx_duration_t prediction_time_span,
                                RequestPresentationTimesCallback callback) override;

  void SetDebugName(std::string debug_name) override {}

 private:
  static constexpr uint32_t kNullResourceId = 0;

  struct Resource {
    Resource(fuchsia::ui::gfx::ResourceArgs args) : args_(std::move(args)) {}

    bool is_material() const { return args_.is_material(); }
    bool is_texture() const {
      return args_.is_image() || args_.is_image_pipe() || args_.is_image_pipe2();
    };
    bool is_shape() const {
      return args_.is_rectangle() || args_.is_rounded_rectangle() || args_.is_circle() ||
             args_.is_mesh();
    };
    bool is_node() const { return args_.is_shape_node() || args_.is_entity_node(); };
    bool is_shape_node() const { return args_.is_shape_node(); };

    bool can_have_children() const {
      return args_.is_view() || args_.is_view3() || args_.is_view_holder() ||
             args_.is_entity_node();
    };
    bool can_have_parts() const { return args_.is_entity_node(); };
    bool can_be_part() const { return args_.is_shape_node(); };
    bool can_have_parent() const { return args_.is_shape_node() || args_.is_entity_node(); }
    bool can_have_material() const { return args_.is_shape_node(); }
    bool can_have_shape() const { return args_.is_shape_node(); }
    bool can_have_transform() const { return args_.is_shape_node() || args_.is_entity_node(); }
    bool can_have_clip_planes() const { return args_.is_entity_node(); }

    fuchsia::ui::gfx::ResourceArgs args_;
    uint32_t parent_ = kNullResourceId;
    std::unordered_set<uint32_t> children_;
    std::unordered_set<uint32_t> parts_;
    std::unique_ptr<fuchsia::ui::gfx::ResourceArgs> shape_args_;
    std::unique_ptr<fuchsia::ui::gfx::Vector3Value> translation_;
    std::unique_ptr<fuchsia::ui::gfx::Vector3Value> scale_;
    std::vector<fuchsia::ui::gfx::Plane3> clip_planes_;
  };

  struct ShapeNode {
    ShapeNode(uint32_t id, fuchsia::ui::gfx::vec3 location, fuchsia::ui::gfx::vec3 extent)
        : id_(id), location_(location), extent_(extent) {}

    bool Intersects(const ShapeNode& other) const {
      return location_.x <= other.location_.x + other.extent_.x &&
             location_.y <= other.location_.y + other.extent_.y &&
             location_.z <= other.location_.z + other.extent_.z &&
             other.location_.x <= location_.x + extent_.x &&
             other.location_.y <= location_.y + extent_.y &&
             other.location_.z <= location_.z + extent_.z;
    }

    uint32_t id_;
    fuchsia::ui::gfx::vec3 location_;
    fuchsia::ui::gfx::vec3 extent_;
  };

  // Gets a weak pointer to this |FakeSession|.
  fxl::WeakPtr<FakeSession> GetWeakThis() { return weak_factory_.GetWeakPtr(); }

  Resource* FindResource(uint32_t id);

  void HandleSetEventMask(uint32_t resource_id, uint32_t event_mask);

  void HandleCreateResource(uint32_t resource_id, fuchsia::ui::gfx::ResourceArgs args);

  void HandleReleaseResource(uint32_t resource_id);

  void HandleAddChild(uint32_t parent_id, uint32_t child_id);

  void HandleSetMaterial(uint32_t node_id, uint32_t material_id);

  void HandleSetTexture(uint32_t material_id, uint32_t texture_id);

  void HandleSetShape(uint32_t node_id, uint32_t shape_id);

  void HandleSetTranslation(uint32_t node_id, const fuchsia::ui::gfx::Vector3Value& value);

  void HandleSetScale(uint32_t node_id, const fuchsia::ui::gfx::Vector3Value& value);

  void HandleSetClipPlanes(uint32_t node_id, std::vector<fuchsia::ui::gfx::Plane3> value);

  // Fake-presents a scene and schedules the next scene presentation.
  void PresentScene();

  void SendGfxEvent(fuchsia::ui::gfx::Event gfx_event);

  // Sets |expected_| to false if z-fighting nodes are found.
  void DetectZFighting();

  void FindShapeNodes(uint32_t node_id, fuchsia::ui::gfx::vec3 translation,
                      fuchsia::ui::gfx::vec3 scale, std::vector<ShapeNode>* shape_nodes);

  fuchsia::sysmem::Allocator* sysmem_allocator_;
  async_dispatcher_t* dispatcher_;
  fidl::Binding<fuchsia::ui::scenic::Session> binding_;
  fuchsia::ui::scenic::SessionListenerPtr listener_;

  std::unordered_map<uint32_t, Resource> resources_by_id_;

  std::unique_ptr<FakeImagePipe> image_pipe_;

  bool dump_expectations_ = false;
  uint32_t expected_black_image_id_ = 0;
  std::unique_ptr<fuchsia::sysmem::ImageFormat_2> expected_black_image_format_;
  std::unique_ptr<fuchsia::sysmem::ImageFormat_2> expected_image_format_;
  std::vector<PacketInfo> expected_packets_info_;
  bool expected_ = true;

  fxl::WeakPtrFactory<FakeSession> weak_factory_;
  zx::time next_presentation_time_;
};

}  // namespace test
}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_SESSION_H_
