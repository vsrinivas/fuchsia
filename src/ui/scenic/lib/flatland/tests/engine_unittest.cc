// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/eventpair.h>

#include <limits>

#include <gtest/gtest.h>

#include "lib/gtest/test_loop_fixture.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/scenic/lib/flatland/flatland.h"
#include "src/ui/scenic/lib/flatland/global_image_data.h"
#include "src/ui/scenic/lib/flatland/global_matrix_data.h"
#include "src/ui/scenic/lib/flatland/global_topology_data.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"
#include "src/ui/scenic/lib/flatland/tests/mock_flatland_presenter.h"
#include "src/ui/scenic/lib/flatland/tests/mock_renderer.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"

using ::testing::_;
using ::testing::Return;

using flatland::FlatlandPresenter;
using flatland::ImageMetadata;
using flatland::LinkSystem;
using flatland::MockFlatlandPresenter;
using flatland::MockRenderer;
using flatland::Renderer;
using flatland::TransformGraph;
using flatland::TransformHandle;
using flatland::UberStruct;
using flatland::UberStructSystem;
using fuchsia::ui::scenic::internal::ContentLink;
using fuchsia::ui::scenic::internal::ContentLinkStatus;
using fuchsia::ui::scenic::internal::ContentLinkToken;
using fuchsia::ui::scenic::internal::GraphLink;
using fuchsia::ui::scenic::internal::GraphLinkToken;
using fuchsia::ui::scenic::internal::LayoutInfo;
using fuchsia::ui::scenic::internal::LinkProperties;

namespace {

class EngineTest : public gtest::TestLoopFixture {
 public:
  EngineTest()
      : uber_struct_system_(std::make_shared<UberStructSystem>()),
        link_system_(std::make_shared<LinkSystem>(uber_struct_system_->GetNextInstanceId())) {}

  void SetUp() override {}

  void TearDown() override {}

  class FakeFlatlandSession {
   public:
    FakeFlatlandSession(const std::shared_ptr<UberStructSystem>& uber_struct_system,
                        const std::shared_ptr<LinkSystem>& link_system, EngineTest* harness)
        : uber_struct_system_(uber_struct_system),
          link_system_(link_system),
          harness_(harness),
          id_(uber_struct_system_->GetNextInstanceId()),
          graph_(id_),
          queue_(uber_struct_system_->AllocateQueueForSession(id_)) {}

    // Use the TransformGraph API to create and manage transforms and their children.
    TransformGraph& graph() { return graph_; }

    // Returns the link_origin for this session.
    TransformHandle GetLinkOrigin() {
      EXPECT_TRUE(parent_link_.has_value());
      return parent_link_.value().parent_link.link_origin;
    }

    // Clears the ParentLink for this session, if one exists.
    void ClearParentLink() { parent_link_.reset(); }

    // Holds the ContentLink and LinkSystem::ChildLink objects since if they fall out of scope,
    // the LinkSystem will delete the link. Tests should add |child_link.link_handle| to their
    // TransformGraphs to use the ChildLink in a topology.
    struct ChildLink {
      fidl::InterfacePtr<ContentLink> content_link;
      LinkSystem::ChildLink child_link;

      // Returns the handle the parent should add as a child in its local topology to include the
      // link in the topology.
      TransformHandle GetLinkHandle() const { return child_link.link_handle; }
    };

    // Links this session to |parent_session| and returns the ChildLink, which should be used with
    // the parent session. If the return value drops out of scope, tests should call
    // ClearParentLink() on this session.
    ChildLink LinkToParent(FakeFlatlandSession& parent_session) {
      // Create the tokens.
      ContentLinkToken parent_token;
      GraphLinkToken child_token;
      EXPECT_EQ(zx::eventpair::create(0, &parent_token.value, &child_token.value), ZX_OK);

      // Create the parent link.
      fidl::InterfacePtr<GraphLink> graph_link;
      LinkSystem::ParentLink parent_link = link_system_->CreateParentLink(
          std::move(child_token), graph_link.NewRequest(), graph_.CreateTransform());

      // Create the child link.
      fidl::InterfacePtr<ContentLink> content_link;
      LinkSystem::ChildLink child_link = link_system_->CreateChildLink(
          std::move(parent_token), LinkProperties(), content_link.NewRequest(),
          parent_session.graph_.CreateTransform());

      // Run the loop to establish the link.
      harness_->RunLoopUntilIdle();

      parent_link_ = ParentLink({
          .graph_link = std::move(graph_link),
          .parent_link = std::move(parent_link),
      });

      return ChildLink({
          .content_link = std::move(content_link),
          .child_link = std::move(child_link),
      });
    }

    // Allocates a new UberStruct with a local_topology rooted at |local_root|. If this session has
    // a ParentLink, the link_origin of that ParentLink will be used instead.
    std::unique_ptr<UberStruct> CreateUberStructWithCurrentTopology(TransformHandle local_root) {
      auto uber_struct = std::make_unique<UberStruct>();

      // Only use the supplied |local_root| if no there is no ParentLink, otherwise use the
      // |link_origin| from the ParentLink.
      const TransformHandle root =
          parent_link_.has_value() ? parent_link_.value().parent_link.link_origin : local_root;

      // Compute the local topology and place it in the UberStruct.
      auto local_topology_data =
          graph_.ComputeAndCleanup(root, std::numeric_limits<uint64_t>::max());
      EXPECT_NE(local_topology_data.iterations, std::numeric_limits<uint64_t>::max());
      EXPECT_TRUE(local_topology_data.cyclical_edges.empty());

      uber_struct->local_topology = local_topology_data.sorted_transforms;

      return uber_struct;
    }

    // Pushes |uber_struct| to the UberStructSystem and updates the system so that it represents
    // this session in the InstanceMap.
    void PushUberStruct(std::unique_ptr<UberStruct> uber_struct) {
      EXPECT_FALSE(uber_struct->local_topology.empty());
      EXPECT_EQ(uber_struct->local_topology[0].handle.GetInstanceId(), id_);

      queue_->Push(/*present_id=*/0, std::move(uber_struct));
      uber_struct_system_->UpdateSessions({{id_, 0}});
    }

   private:
    // Shared systems for all sessions.
    std::shared_ptr<UberStructSystem> uber_struct_system_;
    std::shared_ptr<LinkSystem> link_system_;

    // The test harness to give access to RunLoopUntilIdle().
    EngineTest* harness_;

    // Data specific this session.
    scheduling::SessionId id_;
    TransformGraph graph_;
    std::shared_ptr<UberStructSystem::UberStructQueue> queue_;

    // Holds the GraphLink and LinkSystem::ParentLink objects since if they fall out of scope,
    // the LinkSystem will delete the link. When |parent_link_| has a value, the
    // |parent_link.link_origin| from this object is used as the root TransformHandle.
    struct ParentLink {
      fidl::InterfacePtr<GraphLink> graph_link;
      LinkSystem::ParentLink parent_link;
    };
    std::optional<ParentLink> parent_link_;
  };

  FakeFlatlandSession CreateSession() {
    return FakeFlatlandSession(uber_struct_system_, link_system_, this);
  }

 protected:
  // Systems that are populated with data from Flatland instances.
  const std::shared_ptr<UberStructSystem> uber_struct_system_;
  const std::shared_ptr<LinkSystem> link_system_;
};

}  // namespace

namespace flatland {
namespace test {

TEST_F(EngineTest, ExampleTest) {
  // Create a parent and child session.
  auto parent_session = CreateSession();
  auto child_session = CreateSession();

  // Create a link between the two.
  auto child_link = child_session.LinkToParent(parent_session);

  // Create the root handle for the parent and a handle that will have an image attached.
  const TransformHandle parent_root_handle = parent_session.graph().CreateTransform();
  const TransformHandle parent_image_handle = parent_session.graph().CreateTransform();

  // Add the two children to the parent root: link, then image.
  parent_session.graph().AddChild(parent_root_handle, child_link.GetLinkHandle());
  parent_session.graph().AddChild(parent_root_handle, parent_image_handle);

  // Create an image handle for the child.
  const TransformHandle child_image_handle = child_session.graph().CreateTransform();

  // Attach that image handle to the link_origin.
  child_session.graph().AddChild(child_session.GetLinkOrigin(), child_image_handle);

  // Get an UberStruct for the parent session.
  auto parent_struct = parent_session.CreateUberStructWithCurrentTopology(parent_root_handle);

  // Add an image.
  parent_struct->images[parent_image_handle] = ImageMetadata({
      .vmo_idx = 1,
  });

  // Submit the UberStruct.
  parent_session.PushUberStruct(std::move(parent_struct));

  // Get an UberStruct for the child session. Note that the argument will be ignored anyway.
  auto child_struct =
      child_session.CreateUberStructWithCurrentTopology(child_session.GetLinkOrigin());

  // Add an image.
  child_struct->images[child_image_handle] = ImageMetadata({
      .vmo_idx = 2,
  });

  // Submit the UberStruct.
  child_session.PushUberStruct(std::move(child_struct));

  // Compile global data. In real tests, this will be where you'll tell the Engine to update. The
  // |parent_root_handle| and display scale passed into UpdateLinks() are mostly likely data that
  // will come from the display.
  const auto snapshot = uber_struct_system_->Snapshot();
  const auto links = link_system_->GetResolvedTopologyLinks();
  const auto data = flatland::GlobalTopologyData::ComputeGlobalTopologyData(
      snapshot, links, link_system_->GetInstanceId(), parent_root_handle);
  const auto matrices =
      flatland::ComputeGlobalMatrices(data.topology_vector, data.parent_indices, snapshot);
  const auto rectangles = flatland::ComputeGlobalRectangles(matrices);
  const auto images = flatland::ComputeGlobalImageData(data.topology_vector, snapshot);

  link_system_->UpdateLinks(data.topology_vector, data.child_counts, data.live_handles, matrices,
                            glm::vec2(1.f), snapshot);

  // For example: verify that the images appear in depth-first order, meaning the child image shows
  // up before the parent image since the link comes before the image.
  EXPECT_EQ(images.size(), 2ul);
  EXPECT_EQ(images[0].vmo_idx, 2u);
  EXPECT_EQ(images[1].vmo_idx, 1u);
}

}  // namespace test
}  // namespace flatland
