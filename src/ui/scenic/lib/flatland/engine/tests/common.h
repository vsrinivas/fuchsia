// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_TESTS_COMMON_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_TESTS_COMMON_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-testing/test_loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/eventpair.h>

#include <limits>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lib/async/dispatcher.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/scenic/lib/flatland/engine/display_compositor.h"
#include "src/ui/scenic/lib/flatland/engine/engine_types.h"
#include "src/ui/scenic/lib/flatland/flatland.h"
#include "src/ui/scenic/lib/flatland/global_image_data.h"
#include "src/ui/scenic/lib/flatland/global_matrix_data.h"
#include "src/ui/scenic/lib/flatland/global_topology_data.h"
#include "src/ui/scenic/lib/flatland/renderer/null_renderer.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"
#include "src/ui/scenic/lib/utils/dispatcher_holder.h"

#include <glm/gtx/matrix_transform_2d.hpp>

namespace flatland {

class DisplayCompositorTestBase : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    gtest::RealLoopFixture::SetUp();
    dispatcher_holder_ = std::make_shared<utils::UnownedDispatcherHolder>(dispatcher());
    uber_struct_system_ = std::make_shared<UberStructSystem>();
    link_system_ = std::make_shared<LinkSystem>(uber_struct_system_->GetNextInstanceId());
    async_set_default_dispatcher(dispatcher());
  }

  void TearDown() override {
    uber_struct_system_.reset();
    link_system_.reset();

    // Move the channel to a local variable which will go out of scope
    // and close when this function returns.
    zx::channel local(std::move(local_));
    dispatcher_holder_.reset();
    gtest::RealLoopFixture::TearDown();
  }

  std::vector<RenderData> GenerateDisplayListForTest(
      const std::unordered_map<uint64_t, std::pair<DisplayInfo, TransformHandle>>& display_map) {
    const auto snapshot = uber_struct_system_->Snapshot();
    const auto links = link_system_->GetResolvedTopologyLinks();
    const auto link_system_id = link_system_->GetInstanceId();

    // Gather the flatland data into a vector of rectangle and image data that can be passed to
    // either the display controller directly or to the software renderer.
    std::vector<RenderData> image_list_per_display;
    for (const auto& [display_id, display_data] : display_map) {
      const auto& transform = display_data.second;

      const auto topology_data =
          GlobalTopologyData::ComputeGlobalTopologyData(snapshot, links, link_system_id, transform);
      const auto global_matrices = ComputeGlobalMatrices(topology_data.topology_vector,
                                                         topology_data.parent_indices, snapshot);

      const auto global_sample_regions = ComputeGlobalImageSampleRegions(
          topology_data.topology_vector, topology_data.parent_indices, snapshot);

      const auto global_clip_regions = ComputeGlobalTransformClipRegions(
          topology_data.topology_vector, topology_data.parent_indices, global_matrices, snapshot);

      auto [image_indices, images] = ComputeGlobalImageData(topology_data.topology_vector,
                                                            topology_data.parent_indices, snapshot);

      auto image_rectangles =
          ComputeGlobalRectangles(SelectAttribute(global_matrices, image_indices),
                                  SelectAttribute(global_sample_regions, image_indices),
                                  SelectAttribute(global_clip_regions, image_indices), images);

      link_system_->UpdateLinks(topology_data.topology_vector, topology_data.live_handles,
                                global_matrices, /*device_pixel_ratio*/ glm::vec2(1.0), snapshot);

      CullRectangles(&image_rectangles, &images, display_data.first.dimensions.x,
                     display_data.first.dimensions.y);
      FX_DCHECK(image_rectangles.size() == images.size());

      image_list_per_display.push_back(
          {.rectangles = image_rectangles, .images = images, .display_id = display_id});
    }
    return image_list_per_display;
  }

  class FakeFlatlandSession {
   public:
    FakeFlatlandSession(std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
                        std::shared_ptr<UberStructSystem> uber_struct_system,
                        std::shared_ptr<LinkSystem> link_system, DisplayCompositorTestBase* harness)
        : dispatcher_holder_(std::move(dispatcher_holder)),
          uber_struct_system_(std::move(uber_struct_system)),
          link_system_(std::move(link_system)),
          harness_(harness),
          id_(uber_struct_system_->GetNextInstanceId()),
          graph_(id_),
          queue_(uber_struct_system_->AllocateQueueForSession(id_)) {}

    // Use the TransformGraph API to create and manage transforms and their children.
    TransformGraph& graph() { return graph_; }

    // Returns the LinkToParent::child_transform_handle for this session.
    TransformHandle GetLinkChildTransformHandle() const {
      EXPECT_TRUE(this->link_to_parent_.has_value());
      return this->link_to_parent_.value().link_to_parent.child_transform_handle;
    }

    // Clears the LinkToParent for this session, if one exists.
    void ClearLinkToParent() { link_to_parent_.reset(); }

    // Holds the ChildViewWatcher and LinkSystem::LinkToChild objects since if they fall out of
    // scope, the LinkSystem will delete the link. Tests should add
    // |link_to_parent.internal_link_handle| to their TransformGraphs to use the LinkToChild in a
    // topology.
    struct LinkToChild {
      fidl::InterfacePtr<fuchsia::ui::composition::ChildViewWatcher> child_view_watcher;
      LinkSystem::LinkToChild link_to_child;

      // Returns the handle the parent should add as a child in its local topology to include the
      // link in the topology.
      TransformHandle GetInternalLinkHandle() const { return link_to_child.internal_link_handle; }
    };

    // Links this session to |parent_session| and returns the LinkToChild, which should be used with
    // the parent session. If the return value drops out of scope, tests should call
    // ClearLinkToParent() on this session.
    LinkToChild CreateView(FakeFlatlandSession& parent_session);

    // Allocates a new UberStruct with a local_topology rooted at |local_root|. If this session has
    // a LinkToParent, the child_transform_handle of that LinkToParent will be used instead.
    std::unique_ptr<UberStruct> CreateUberStructWithCurrentTopology(TransformHandle local_root);

    // Pushes |uber_struct| to the UberStructSystem and updates the system so that it represents
    // this session in the InstanceMap.
    void PushUberStruct(std::unique_ptr<UberStruct> uber_struct);

   private:
    std::shared_ptr<utils::DispatcherHolder> dispatcher_holder_;

    // Shared systems for all sessions.
    std::shared_ptr<UberStructSystem> uber_struct_system_;
    std::shared_ptr<LinkSystem> link_system_;

    // The test harness to give access to RunLoopUntilIdle().
    DisplayCompositorTestBase* harness_;

    // Data specific to this session.
    scheduling::SessionId id_;
    TransformGraph graph_;
    std::shared_ptr<UberStructSystem::UberStructQueue> queue_;

    // Holds the ParentViewportWatcher and LinkSystem::LinkToParent objects since if they fall out
    // of scope, the LinkSystem will delete the link. When |link_to_parent_| has a value, the
    // |link_to_parent.child_transform_handle| from this object is used as the root TransformHandle.
    struct LinkToParent {
      fidl::InterfacePtr<fuchsia::ui::composition::ParentViewportWatcher> parent_viewport_watcher;
      LinkSystem::LinkToParent link_to_parent;
    };
    std::optional<LinkToParent> link_to_parent_;
  };

  FakeFlatlandSession CreateSession() {
    return FakeFlatlandSession(dispatcher_holder_, uber_struct_system_, link_system_, this);
  }

 protected:
  const std::shared_ptr<UberStructSystem>& uber_struct_system() const {
    return uber_struct_system_;
  }

  const std::shared_ptr<LinkSystem>& link_system() const { return link_system_; }

 private:
  std::shared_ptr<utils::DispatcherHolder> dispatcher_holder_;

  // Systems that are populated with data from Flatland instances.
  std::shared_ptr<UberStructSystem> uber_struct_system_;
  std::shared_ptr<LinkSystem> link_system_;
  zx::channel local_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_TESTS_COMMON_H_
