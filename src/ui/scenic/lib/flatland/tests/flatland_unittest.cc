// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/flatland.h"

#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>

#include <limits>

#include <gtest/gtest.h>

#include "fuchsia/ui/scenic/internal/cpp/fidl.h"
#include "lib/gtest/test_loop_fixture.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/scenic/lib/flatland/global_image_data.h"
#include "src/ui/scenic/lib/flatland/global_matrix_data.h"
#include "src/ui/scenic/lib/flatland/global_topology_data.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"
#include "src/ui/scenic/lib/flatland/tests/mock_flatland_presenter.h"
#include "src/ui/scenic/lib/flatland/tests/mock_renderer.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"

#include <glm/gtc/epsilon.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>

using ::testing::_;
using ::testing::Return;

using BufferCollectionId = flatland::Flatland::BufferCollectionId;
using ContentId = flatland::Flatland::ContentId;
using GlobalBufferCollectionId = flatland::GlobalBufferCollectionId;
using TransformId = flatland::Flatland::TransformId;
using flatland::BufferCollectionMetadata;
using flatland::Flatland;
using flatland::FlatlandPresenter;
using flatland::GlobalImageVector;
using flatland::GlobalMatrixVector;
using flatland::GlobalRectangleVector;
using flatland::GlobalTopologyData;
using flatland::ImageMetadata;
using flatland::LinkSystem;
using flatland::MockFlatlandPresenter;
using flatland::MockRenderer;
using flatland::Renderer;
using flatland::TransformGraph;
using flatland::TransformHandle;
using flatland::UberStructSystem;
using fuchsia::ui::scenic::internal::ContentLink;
using fuchsia::ui::scenic::internal::ContentLinkStatus;
using fuchsia::ui::scenic::internal::ContentLinkToken;
using fuchsia::ui::scenic::internal::Flatland_Present_Result;
using fuchsia::ui::scenic::internal::GraphLink;
using fuchsia::ui::scenic::internal::GraphLinkToken;
using fuchsia::ui::scenic::internal::ImageProperties;
using fuchsia::ui::scenic::internal::LayoutInfo;
using fuchsia::ui::scenic::internal::LinkProperties;
using fuchsia::ui::scenic::internal::Orientation;
using fuchsia::ui::scenic::internal::Vec2;

namespace {

// Convenience struct for the PRESENT_WITH_ARGS macro to avoid having to update it every time
// a new argument is added to Flatland::Present().
struct PresentArgs {
  std::vector<zx::event> acquire_fences;
  std::vector<zx::event> release_fences;
};

}  // namespace

// These macros works like functions that check a variety of conditions, but if those conditions
// fail, the line number for the failure will appear in-line rather than in a function.

// This macro calls Present() on a Flatland object and immediately triggers the session update
// for all sessions so that changes from that Present() are visible in global systems. This is
// primarily useful for testing the user-facing Flatland API.
//
// This macro must be used within a test using the FlatlandTest harness.
//
// |flatland| is a Flatland object constructed with the MockFlatlandPresenter owned by the
// FlatlandTest harness. |expect_success| should be false if the call to Present() is expected to
// trigger an error.
#define PRESENT_WITH_ARGS(flatland, args, expect_success)                               \
  {                                                                                     \
    bool processed_callback = false;                                                    \
    flatland.Present(std::move(args.acquire_fences), std::move(args.release_fences),    \
                     [&](Flatland_Present_Result result) {                              \
                       EXPECT_EQ(!expect_success, result.is_err());                     \
                       if (expect_success) {                                            \
                         EXPECT_EQ(1u, result.response().num_presents_remaining);       \
                       } else {                                                         \
                         EXPECT_EQ(fuchsia::ui::scenic::internal::Error::BAD_OPERATION, \
                                   result.err());                                       \
                       }                                                                \
                       processed_callback = true;                                       \
                     });                                                                \
    EXPECT_TRUE(processed_callback);                                                    \
    /* Even with no acquire_fences, UberStructs updates queue on the dispatcher. */     \
    RunLoopUntilIdle();                                                                 \
    mock_flatland_presenter_->ApplySessionUpdatesAndSignalFences();                     \
  }

// Identical to PRESENT_WITH_ARGS, but supplies an empty PresentArgs to the Present() call.
#define PRESENT(flatland, expect_success) \
  { PRESENT_WITH_ARGS(flatland, PresentArgs(), expect_success); }

// |global_topology_data| is a GlobalTopologyData object. |global_matrix_vector| is a
// GlobalMatrixVector generated from the same set of UberStructs and topology data. |target_handle|
// is the TransformHandle of the matrix to compare. |expected_matrix| is the expected value of
// that matrix.
#define EXPECT_MATRIX(global_topology_data, global_matrix_vector, target_handle, expected_matrix) \
  {                                                                                               \
    ASSERT_EQ(global_topology_data.live_handles.count(target_handle), 1u);                        \
    int index = -1;                                                                               \
    for (size_t i = 0; i < global_topology_data.topology_vector.size(); ++i) {                    \
      if (global_topology_data.topology_vector[i] == target_handle) {                             \
        index = i;                                                                                \
        break;                                                                                    \
      }                                                                                           \
    }                                                                                             \
    ASSERT_NE(index, -1);                                                                         \
    const glm::mat3& matrix = global_matrix_vector[index];                                        \
    for (size_t i = 0; i < 3; ++i) {                                                              \
      for (size_t j = 0; j < 3; ++j) {                                                            \
        EXPECT_FLOAT_EQ(matrix[i][j], expected_matrix[i][j]) << " row " << j << " column " << i;  \
      }                                                                                           \
    }                                                                                             \
  }

namespace {

// TODO(fxbug.dev/56879): consolidate the following 3 helper functions when splitting escher into
// multiple libraries.

zx::event CreateEvent() {
  zx::event event;
  FX_CHECK(zx::event::create(0, &event) == ZX_OK);
  return event;
}

std::vector<zx::event> CreateEventArray(size_t n) {
  std::vector<zx::event> events;
  for (size_t i = 0; i < n; i++) {
    events.push_back(CreateEvent());
  }
  return events;
}

zx::event CopyEvent(const zx::event& event) {
  zx::event event_copy;
  if (event.duplicate(ZX_RIGHT_SAME_RIGHTS, &event_copy) != ZX_OK) {
    FX_LOGS(ERROR) << "Copying zx::event failed.";
  }
  return event_copy;
}

bool IsEventSignaled(const zx::event& fence, zx_signals_t signal) {
  zx_signals_t pending = 0u;
  fence.wait_one(signal, zx::time(), &pending);
  return (pending & signal) != 0u;
}

const float kDefaultSize = 1.f;
const glm::vec2 kDefaultPixelScale = {1.f, 1.f};

float GetOrientationAngle(fuchsia::ui::scenic::internal::Orientation orientation) {
  switch (orientation) {
    case Orientation::CCW_0_DEGREES:
      return 0.f;
    case Orientation::CCW_90_DEGREES:
      return glm::half_pi<float>();
    case Orientation::CCW_180_DEGREES:
      return glm::pi<float>();
    case Orientation::CCW_270_DEGREES:
      return glm::three_over_two_pi<float>();
  }
}

class FlatlandTest : public gtest::TestLoopFixture {
 public:
  FlatlandTest()
      : uber_struct_system_(std::make_shared<UberStructSystem>()),
        link_system_(std::make_shared<LinkSystem>(uber_struct_system_->GetNextInstanceId())) {}

  void SetUp() override {
    mock_flatland_presenter_ = new MockFlatlandPresenter(uber_struct_system_.get());
    flatland_presenter_ = std::shared_ptr<FlatlandPresenter>(mock_flatland_presenter_);

    mock_renderer_ = new MockRenderer();
    renderer_ = std::shared_ptr<Renderer>(mock_renderer_);
  }

  void TearDown() override {
    RunLoopUntilIdle();

    // Trigger cleanup of Flatland sessions.
    uber_struct_system_->UpdateSessions({});

    auto snapshot = uber_struct_system_->Snapshot();
    EXPECT_TRUE(snapshot.empty());
    EXPECT_EQ(uber_struct_system_->GetSessionCount(), 0ul);

    auto link_topologies = link_system_->GetResolvedTopologyLinks();
    EXPECT_TRUE(link_topologies.empty());

    renderer_.reset();
    flatland_presenter_.reset();
  }

  Flatland CreateFlatland() {
    fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator.NewRequest().TakeChannel().release());
    FX_DCHECK(status == ZX_OK);
    auto session_id = scheduling::GetNextSessionId();
    return Flatland(session_id, flatland_presenter_, renderer_, link_system_,
                    uber_struct_system_->AllocateQueueForSession(session_id),
                    std::move(sysmem_allocator));
  }

  void SetDisplayPixelScale(const glm::vec2& pixel_scale) { display_pixel_scale_ = pixel_scale; }

  // The parent transform must be a topology root or ComputeGlobalTopologyData() will crash.
  bool IsDescendantOf(TransformHandle parent, TransformHandle child) {
    auto snapshot = uber_struct_system_->Snapshot();
    auto links = link_system_->GetResolvedTopologyLinks();
    auto data = GlobalTopologyData::ComputeGlobalTopologyData(
        snapshot, links, link_system_->GetInstanceId(), parent);
    for (auto handle : data.topology_vector) {
      if (handle == child) {
        return true;
      }
    }
    return false;
  }

  // The render loop computes this data to hand off to the Renderer, so we return it here for test
  // validation purposes.
  struct GlobalFlatlandData {
    GlobalTopologyData topology_data;
    GlobalMatrixVector matrix_vector;
    GlobalRectangleVector rectangle_vector;
    GlobalImageVector image_vector;
  };

  // Processing the main loop involves generating a global topology. For testing, the root transform
  // is provided directly to this function.
  GlobalFlatlandData ProcessMainLoop(TransformHandle root_transform) {
    // Run the looper in case there are queued commands in, e.g., ObjectLinker.
    RunLoopUntilIdle();

    // This is a replica of the core render loop.
    const auto snapshot = uber_struct_system_->Snapshot();
    const auto links = link_system_->GetResolvedTopologyLinks();
    const auto data = GlobalTopologyData::ComputeGlobalTopologyData(
        snapshot, links, link_system_->GetInstanceId(), root_transform);
    const auto matrices =
        flatland::ComputeGlobalMatrices(data.topology_vector, data.parent_indices, snapshot);
    const auto rectangles = flatland::ComputeGlobalRectangles(matrices);
    const auto images = flatland::ComputeGlobalImageData(data.topology_vector, snapshot);

    link_system_->UpdateLinks(data.topology_vector, data.child_counts, data.live_handles, matrices,
                              display_pixel_scale_, snapshot);

    // Run the looper again to process any queued FIDL events (i.e., Link callbacks).
    RunLoopUntilIdle();

    return {.topology_data = std::move(data),
            .matrix_vector = std::move(matrices),
            .rectangle_vector = std::move(rectangles),
            .image_vector = std::move(images)};
  }

  void CreateLink(Flatland* parent, Flatland* child, ContentId id,
                  fidl::InterfacePtr<ContentLink>* content_link,
                  fidl::InterfacePtr<GraphLink>* graph_link) {
    ContentLinkToken parent_token;
    GraphLinkToken child_token;
    ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

    LinkProperties properties;
    properties.set_logical_size({kDefaultSize, kDefaultSize});
    parent->CreateLink(id, std::move(parent_token), std::move(properties),
                       content_link->NewRequest());
    child->LinkToParent(std::move(child_token), graph_link->NewRequest());
    PRESENT((*parent), true);
    PRESENT((*child), true);
  }

  // Creates an image in |flatland| with the specified |image_id| and backing properties, and
  // returns the Renderer-generated GlobalBufferCollectionId that will be in the ImageMetadata
  // struct for that Image.
  GlobalBufferCollectionId CreateImage(Flatland* flatland, ContentId image_id,
                                       BufferCollectionId collection_id,
                                       ImageProperties properties) {
    GlobalBufferCollectionId global_collection_id = next_global_collection_id_++;

    EXPECT_CALL(*mock_renderer_, RegisterTextureCollection(_, _))
        .WillOnce(Return(global_collection_id));

    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
    flatland->RegisterBufferCollection(collection_id, std::move(token));

    // Ensure all buffer constraints are valid for the desired image by generating constraints based
    // on the image properties.
    BufferCollectionMetadata metadata;
    metadata.vmo_count = 1;

    FX_DCHECK(properties.has_width());
    metadata.image_constraints.min_coded_width = properties.width();
    metadata.image_constraints.max_coded_width = properties.width();

    FX_DCHECK(properties.has_height());
    metadata.image_constraints.min_coded_height = properties.height();
    metadata.image_constraints.max_coded_height = properties.height();

    EXPECT_CALL(*mock_renderer_, Validate(global_collection_id)).WillOnce(Return(metadata));

    flatland->CreateImage(image_id, collection_id, 0, std::move(properties));
    PRESENT((*flatland), true);

    return global_collection_id;
  }

 protected:
  MockFlatlandPresenter* mock_flatland_presenter_;
  MockRenderer* mock_renderer_;
  const std::shared_ptr<UberStructSystem> uber_struct_system_;

 private:
  std::shared_ptr<FlatlandPresenter> flatland_presenter_;
  std::shared_ptr<Renderer> renderer_;
  const std::shared_ptr<LinkSystem> link_system_;
  glm::vec2 display_pixel_scale_ = kDefaultPixelScale;
  std::atomic<BufferCollectionId> next_global_collection_id_ = 1;
};

}  // namespace

namespace flatland {
namespace test {

TEST_F(FlatlandTest, PresentShouldReturnOne) {
  Flatland flatland = CreateFlatland();
  PRESENT(flatland, true);
}

TEST_F(FlatlandTest, PresentWaitsForAcquireFences) {
  Flatland flatland = CreateFlatland();

  // Create two events to serve as acquire fences.
  PresentArgs args;
  args.acquire_fences = CreateEventArray(2);
  auto acquire1_copy = CopyEvent(args.acquire_fences[0]);
  auto acquire2_copy = CopyEvent(args.acquire_fences[1]);

  // Create an event to serve as a release fence.
  args.release_fences = CreateEventArray(1);
  auto release_copy = CopyEvent(args.release_fences[0]);

  // The UberStructSystem shouldn't update when the Present includes acquire fences that haven't
  // been reached, and the release fence shouldn't be signaled.
  PRESENT_WITH_ARGS(flatland, std::move(args), true);
  RunLoopUntilIdle();

  auto snapshot = uber_struct_system_->Snapshot();
  EXPECT_TRUE(snapshot.empty());

  EXPECT_FALSE(IsEventSignaled(release_copy, ZX_EVENT_SIGNALED));

  // Signal the second fence and ensure the UberStructSystem doesn't update and the release fence
  // isn't signaled. Signal order doesn't matter.
  acquire2_copy.signal(0, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  mock_flatland_presenter_->ApplySessionUpdatesAndSignalFences();

  snapshot = uber_struct_system_->Snapshot();
  EXPECT_TRUE(snapshot.empty());

  EXPECT_FALSE(IsEventSignaled(release_copy, ZX_EVENT_SIGNALED));

  // Signal the first fence and ensure the UberStructSystem contains an UberStruct for the
  // instance and the release fence is signaled.
  acquire1_copy.signal(0, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  mock_flatland_presenter_->ApplySessionUpdatesAndSignalFences();

  snapshot = uber_struct_system_->Snapshot();
  EXPECT_EQ(snapshot.size(), 1ul);
  EXPECT_NE(snapshot.find(flatland.GetRoot().GetInstanceId()), snapshot.end());

  EXPECT_TRUE(IsEventSignaled(release_copy, ZX_EVENT_SIGNALED));
}

TEST_F(FlatlandTest, PresentWithSignaledFencesUpdatesImmediately) {
  Flatland flatland = CreateFlatland();

  // Create an event to serve as the acquire fence.
  PresentArgs args;
  args.acquire_fences = CreateEventArray(1);
  auto acquire_copy = CopyEvent(args.acquire_fences[0]);

  // Create an event to serve as a release fence.
  args.release_fences = CreateEventArray(1);
  auto release_copy = CopyEvent(args.release_fences[0]);

  // Signal the event before the Present() call.
  acquire_copy.signal(0, ZX_EVENT_SIGNALED);

  // The UberStructSystem should update immediately and the release fence should be signaled.
  PRESENT_WITH_ARGS(flatland, std::move(args), true);
  RunLoopUntilIdle();

  auto snapshot = uber_struct_system_->Snapshot();
  EXPECT_EQ(snapshot.size(), 1ul);
  EXPECT_NE(snapshot.find(flatland.GetRoot().GetInstanceId()), snapshot.end());

  EXPECT_TRUE(IsEventSignaled(release_copy, ZX_EVENT_SIGNALED));
}

TEST_F(FlatlandTest, PresentsUpdateInCallOrder) {
  Flatland flatland = CreateFlatland();

  // Create an event to serve as the acquire fence for the first Present().
  PresentArgs args1;
  args1.acquire_fences = CreateEventArray(1);
  auto acquire1_copy = CopyEvent(args1.acquire_fences[0]);

  // Create an event to serve as a release fence.
  args1.release_fences = CreateEventArray(1);
  auto release1_copy = CopyEvent(args1.release_fences[0]);

  // Present, but do not signal the fence, and ensure the UberStructSystem is empty and the release
  // fence is unsignaled.
  PRESENT_WITH_ARGS(flatland, std::move(args1), true);
  RunLoopUntilIdle();

  auto snapshot = uber_struct_system_->Snapshot();
  EXPECT_TRUE(snapshot.empty());

  EXPECT_FALSE(IsEventSignaled(release1_copy, ZX_EVENT_SIGNALED));

  // Create a transform and make it the root.
  const TransformId kId = 1;

  flatland.CreateTransform(kId);
  flatland.SetRootTransform(kId);

  // Create another event to serve as the acquire fence for the second Present().
  PresentArgs args2;
  args2.acquire_fences = CreateEventArray(1);
  auto acquire2_copy = CopyEvent(args2.acquire_fences[0]);

  // Create an event to serve as a release fence.
  args2.release_fences = CreateEventArray(1);
  auto release2_copy = CopyEvent(args2.release_fences[0]);

  // Present, but do not signal the fence, and ensure the UberStructSystem is still empty and both
  // release fences are unsignaled.
  PRESENT_WITH_ARGS(flatland, std::move(args2), true);
  RunLoopUntilIdle();

  snapshot = uber_struct_system_->Snapshot();
  EXPECT_TRUE(snapshot.empty());

  EXPECT_FALSE(IsEventSignaled(release1_copy, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(IsEventSignaled(release2_copy, ZX_EVENT_SIGNALED));

  // Signal the fence for the second Present(). Since the first one is not done, there should still
  // be no UberStruct for the instance, and neither fence should be signaled.
  acquire2_copy.signal(0, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  mock_flatland_presenter_->ApplySessionUpdatesAndSignalFences();

  snapshot = uber_struct_system_->Snapshot();
  EXPECT_TRUE(snapshot.empty());

  EXPECT_FALSE(IsEventSignaled(release1_copy, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(IsEventSignaled(release2_copy, ZX_EVENT_SIGNALED));

  // Signal the fence for the first Present(). This should trigger both Presents(), resulting in an
  // UberStruct with a 2-element topology: the local root, and kId.
  acquire1_copy.signal(0, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  mock_flatland_presenter_->ApplySessionUpdatesAndSignalFences();

  snapshot = uber_struct_system_->Snapshot();
  EXPECT_EQ(snapshot.size(), 1ul);

  auto uber_struct_kv = snapshot.find(flatland.GetRoot().GetInstanceId());
  EXPECT_NE(uber_struct_kv, snapshot.end());
  EXPECT_EQ(uber_struct_kv->second->local_topology.size(), 2ul);

  EXPECT_TRUE(IsEventSignaled(release1_copy, ZX_EVENT_SIGNALED));
  EXPECT_TRUE(IsEventSignaled(release2_copy, ZX_EVENT_SIGNALED));
}

TEST_F(FlatlandTest, CreateAndReleaseTransformValidCases) {
  Flatland flatland = CreateFlatland();

  const TransformId kId1 = 1;
  const TransformId kId2 = 2;

  // Create two transforms.
  flatland.CreateTransform(kId1);
  flatland.CreateTransform(kId2);
  PRESENT(flatland, true);

  // Clear, then create two transforms in the other order.
  flatland.ClearGraph();
  flatland.CreateTransform(kId2);
  flatland.CreateTransform(kId1);
  PRESENT(flatland, true);

  // Clear, create and release transforms, non-overlapping.
  flatland.ClearGraph();
  flatland.CreateTransform(kId1);
  flatland.ReleaseTransform(kId1);
  flatland.CreateTransform(kId2);
  flatland.ReleaseTransform(kId2);
  PRESENT(flatland, true);

  // Clear, create and release transforms, nested.
  flatland.ClearGraph();
  flatland.CreateTransform(kId2);
  flatland.CreateTransform(kId1);
  flatland.ReleaseTransform(kId1);
  flatland.ReleaseTransform(kId2);
  PRESENT(flatland, true);

  // Reuse the same id, legally, in a single present call.
  flatland.CreateTransform(kId1);
  flatland.ReleaseTransform(kId1);
  flatland.CreateTransform(kId1);
  flatland.ClearGraph();
  flatland.CreateTransform(kId1);
  PRESENT(flatland, true);

  // Create and clear, overlapping, with multiple present calls.
  flatland.ClearGraph();
  flatland.CreateTransform(kId2);
  PRESENT(flatland, true);
  flatland.CreateTransform(kId1);
  flatland.ReleaseTransform(kId2);
  PRESENT(flatland, true);
  flatland.ReleaseTransform(kId1);
  PRESENT(flatland, true);
}

TEST_F(FlatlandTest, CreateAndReleaseTransformErrorCases) {
  Flatland flatland = CreateFlatland();

  const TransformId kId1 = 1;
  const TransformId kId2 = 2;

  // Zero is not a valid transform id.
  flatland.CreateTransform(0);
  PRESENT(flatland, false);
  flatland.ReleaseTransform(0);
  PRESENT(flatland, false);

  // Double creation is an error.
  flatland.CreateTransform(kId1);
  flatland.CreateTransform(kId1);
  PRESENT(flatland, false);

  // Releasing a non-existent transform is an error.
  flatland.ReleaseTransform(kId2);
  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, AddAndRemoveChildValidCases) {
  Flatland flatland = CreateFlatland();

  const TransformId kIdParent = 1;
  const TransformId kIdChild1 = 2;
  const TransformId kIdChild2 = 3;
  const TransformId kIdGrandchild = 4;

  flatland.CreateTransform(kIdParent);
  flatland.CreateTransform(kIdChild1);
  flatland.CreateTransform(kIdChild2);
  flatland.CreateTransform(kIdGrandchild);
  PRESENT(flatland, true);

  // Add and remove.
  flatland.AddChild(kIdParent, kIdChild1);
  flatland.RemoveChild(kIdParent, kIdChild1);
  PRESENT(flatland, true);

  // Add two children.
  flatland.AddChild(kIdParent, kIdChild1);
  flatland.AddChild(kIdParent, kIdChild2);
  PRESENT(flatland, true);

  // Remove two children.
  flatland.RemoveChild(kIdParent, kIdChild1);
  flatland.RemoveChild(kIdParent, kIdChild2);
  PRESENT(flatland, true);

  // Add two-deep hierarchy.
  flatland.AddChild(kIdParent, kIdChild1);
  flatland.AddChild(kIdChild1, kIdGrandchild);
  PRESENT(flatland, true);

  // Add sibling.
  flatland.AddChild(kIdParent, kIdChild2);
  PRESENT(flatland, true);

  // Add shared grandchild (deadly diamond dependency).
  flatland.AddChild(kIdChild2, kIdGrandchild);
  PRESENT(flatland, true);

  // Remove original deep-hierarchy.
  flatland.RemoveChild(kIdChild1, kIdGrandchild);
  PRESENT(flatland, true);
}

TEST_F(FlatlandTest, AddAndRemoveChildErrorCases) {
  Flatland flatland = CreateFlatland();

  const TransformId kIdParent = 1;
  const TransformId kIdChild = 2;
  const TransformId kIdNotCreated = 3;

  // Setup.
  flatland.CreateTransform(kIdParent);
  flatland.CreateTransform(kIdChild);
  flatland.AddChild(kIdParent, kIdChild);
  PRESENT(flatland, true);

  // Zero is not a valid transform id.
  flatland.AddChild(0, 0);
  PRESENT(flatland, false);
  flatland.AddChild(kIdParent, 0);
  PRESENT(flatland, false);
  flatland.AddChild(0, kIdChild);
  PRESENT(flatland, false);

  // Child does not exist.
  flatland.AddChild(kIdParent, kIdNotCreated);
  PRESENT(flatland, false);
  flatland.RemoveChild(kIdParent, kIdNotCreated);
  PRESENT(flatland, false);

  // Parent does not exist.
  flatland.AddChild(kIdNotCreated, kIdChild);
  PRESENT(flatland, false);
  flatland.RemoveChild(kIdNotCreated, kIdChild);
  PRESENT(flatland, false);

  // Child is already a child of parent.
  flatland.AddChild(kIdParent, kIdChild);
  PRESENT(flatland, false);

  // Both nodes exist, but not in the correct relationship.
  flatland.RemoveChild(kIdChild, kIdParent);
  PRESENT(flatland, false);
}

// Test that Transforms can be children to multiple different parents.
TEST_F(FlatlandTest, MultichildUsecase) {
  Flatland flatland = CreateFlatland();

  const TransformId kIdParent1 = 1;
  const TransformId kIdParent2 = 2;
  const TransformId kIdChild1 = 3;
  const TransformId kIdChild2 = 4;
  const TransformId kIdChild3 = 5;

  // Setup
  flatland.CreateTransform(kIdParent1);
  flatland.CreateTransform(kIdParent2);
  flatland.CreateTransform(kIdChild1);
  flatland.CreateTransform(kIdChild2);
  flatland.CreateTransform(kIdChild3);
  PRESENT(flatland, true);

  // Add all children to first parent.
  flatland.AddChild(kIdParent1, kIdChild1);
  flatland.AddChild(kIdParent1, kIdChild2);
  flatland.AddChild(kIdParent1, kIdChild3);
  PRESENT(flatland, true);

  // Add all children to second parent.
  flatland.AddChild(kIdParent2, kIdChild1);
  flatland.AddChild(kIdParent2, kIdChild2);
  flatland.AddChild(kIdParent2, kIdChild3);
  PRESENT(flatland, true);
}

// Test that Present() fails if it detects a graph cycle.
TEST_F(FlatlandTest, CycleDetector) {
  Flatland flatland = CreateFlatland();

  const TransformId kId1 = 1;
  const TransformId kId2 = 2;
  const TransformId kId3 = 3;
  const TransformId kId4 = 4;

  // Create an immediate cycle.
  {
    flatland.CreateTransform(kId1);
    flatland.AddChild(kId1, kId1);
    PRESENT(flatland, false);
  }

  // Create a legal chain of depth one.
  // Then, create a cycle of length 2.
  {
    flatland.ClearGraph();
    flatland.CreateTransform(kId1);
    flatland.CreateTransform(kId2);
    flatland.AddChild(kId1, kId2);
    PRESENT(flatland, true);

    flatland.AddChild(kId2, kId1);
    PRESENT(flatland, false);
  }

  // Create two legal chains of length one.
  // Then, connect each chain into a cycle of length four.
  {
    flatland.ClearGraph();
    flatland.CreateTransform(kId1);
    flatland.CreateTransform(kId2);
    flatland.CreateTransform(kId3);
    flatland.CreateTransform(kId4);
    flatland.AddChild(kId1, kId2);
    flatland.AddChild(kId3, kId4);
    PRESENT(flatland, true);

    flatland.AddChild(kId2, kId3);
    flatland.AddChild(kId4, kId1);
    PRESENT(flatland, false);
  }

  // Create a cycle, where the root is not involved in the cycle.
  {
    flatland.ClearGraph();
    flatland.CreateTransform(kId1);
    flatland.CreateTransform(kId2);
    flatland.CreateTransform(kId3);
    flatland.CreateTransform(kId4);

    flatland.AddChild(kId1, kId2);
    flatland.AddChild(kId2, kId3);
    flatland.AddChild(kId3, kId2);
    flatland.AddChild(kId3, kId4);

    flatland.SetRootTransform(kId1);
    flatland.ReleaseTransform(kId1);
    flatland.ReleaseTransform(kId2);
    flatland.ReleaseTransform(kId3);
    flatland.ReleaseTransform(kId4);
    PRESENT(flatland, false);
  }
}

TEST_F(FlatlandTest, SetRootTransform) {
  Flatland flatland = CreateFlatland();

  const TransformId kId1 = 1;
  const TransformId kIdNotCreated = 2;

  flatland.CreateTransform(kId1);
  PRESENT(flatland, true);

  // Even with no root transform, so clearing it is not an error.
  flatland.SetRootTransform(0);
  PRESENT(flatland, true);

  // Setting the root to an unknown transform is an error.
  flatland.SetRootTransform(kIdNotCreated);
  PRESENT(flatland, false);

  flatland.SetRootTransform(kId1);
  PRESENT(flatland, true);

  // Setting the root to a non-existent transform does not clear the root. Verify this using the
  // global topology data.
  auto data = ProcessMainLoop(flatland.GetRoot());
  EXPECT_EQ(data.topology_data.topology_vector.size(), 2ul);

  flatland.SetRootTransform(kIdNotCreated);
  PRESENT(flatland, false);

  // The previous Present() fails, so we Present() again to ensure the UberStruct is updated,
  // even though we expect no changes.
  PRESENT(flatland, true);

  data = ProcessMainLoop(flatland.GetRoot());
  EXPECT_EQ(data.topology_data.topology_vector.size(), 2ul);

  // Releasing the root is allowed.
  flatland.ReleaseTransform(kId1);
  PRESENT(flatland, true);

  // Clearing the root after release is also allowed.
  flatland.SetRootTransform(0);
  PRESENT(flatland, true);

  // Setting the root to a released transform is not allowed.
  flatland.SetRootTransform(kId1);
  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, SetTranslationErrorCases) {
  Flatland flatland = CreateFlatland();

  const TransformId kIdNotCreated = 1;

  // Zero is not a valid transform ID.
  flatland.SetTranslation(0, {1.f, 2.f});
  PRESENT(flatland, false);

  // Transform does not exist.
  flatland.SetTranslation(kIdNotCreated, {1.f, 2.f});
  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, SetOrientationErrorCases) {
  Flatland flatland = CreateFlatland();

  const TransformId kIdNotCreated = 1;

  // Zero is not a valid transform ID.
  flatland.SetOrientation(0, Orientation::CCW_90_DEGREES);
  PRESENT(flatland, false);

  // Transform does not exist.
  flatland.SetOrientation(kIdNotCreated, Orientation::CCW_90_DEGREES);
  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, SetScaleErrorCases) {
  Flatland flatland = CreateFlatland();

  const TransformId kIdNotCreated = 1;

  // Zero is not a valid transform ID.
  flatland.SetScale(0, {1.f, 2.f});
  PRESENT(flatland, false);

  // Transform does not exist.
  flatland.SetScale(kIdNotCreated, {1.f, 2.f});
  PRESENT(flatland, false);
}

// Test that changing geometric transform properties affects the global matrix of child Transforms.
TEST_F(FlatlandTest, SetGeometricTransformProperties) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  const ContentId kLinkId1 = 1;

  fidl::InterfacePtr<ContentLink> content_link;
  fidl::InterfacePtr<GraphLink> graph_link;
  CreateLink(&parent, &child, kLinkId1, &content_link, &graph_link);
  RunLoopUntilIdle();

  // Create two Transforms in the parent to ensure properties cascade down children.
  const TransformId kId1 = 1;
  const TransformId kId2 = 2;

  parent.CreateTransform(kId1);
  parent.CreateTransform(kId2);

  parent.SetRootTransform(kId1);
  parent.AddChild(kId1, kId2);

  parent.SetContentOnTransform(kLinkId1, kId2);

  PRESENT(parent, true);

  // With no properties set, the child root has an identity transform.
  auto data = ProcessMainLoop(parent.GetRoot());
  EXPECT_MATRIX(data.topology_data, data.matrix_vector, child.GetRoot(), glm::mat3());

  // Set up one property per transform. Set up kId2 before kId1 to ensure the hierarchy is used.
  parent.SetScale(kId2, {2.f, 3.f});
  parent.SetTranslation(kId1, {1.f, 2.f});
  PRESENT(parent, true);

  // The operations should be applied in the following order:
  // - Translation on kId1
  // - Scale on kId2
  glm::mat3 expected_matrix = glm::mat3();
  expected_matrix = glm::translate(expected_matrix, {1.f, 2.f});
  expected_matrix = glm::scale(expected_matrix, {2.f, 3.f});

  data = ProcessMainLoop(parent.GetRoot());
  EXPECT_MATRIX(data.topology_data, data.matrix_vector, child.GetRoot(), expected_matrix);

  // Fill out the remaining properties on both transforms.
  parent.SetOrientation(kId1, Orientation::CCW_90_DEGREES);
  parent.SetScale(kId1, {4.f, 5.f});
  parent.SetTranslation(kId2, {6.f, 7.f});
  parent.SetOrientation(kId2, Orientation::CCW_270_DEGREES);
  PRESENT(parent, true);

  // The operations should be applied in the following order:
  // - Translation on kId1
  // - Orientation on kId1
  // - Scale on kId1
  // - Translation on kId2
  // - Orientation on kId2
  // - Scale on kId2
  expected_matrix = glm::mat3();
  expected_matrix = glm::translate(expected_matrix, {1.f, 2.f});
  expected_matrix = glm::rotate(expected_matrix, GetOrientationAngle(Orientation::CCW_90_DEGREES));
  expected_matrix = glm::scale(expected_matrix, {4.f, 5.f});
  expected_matrix = glm::translate(expected_matrix, {6.f, 7.f});
  expected_matrix = glm::rotate(expected_matrix, GetOrientationAngle(Orientation::CCW_270_DEGREES));
  expected_matrix = glm::scale(expected_matrix, {2.f, 3.f});

  data = ProcessMainLoop(parent.GetRoot());
  EXPECT_MATRIX(data.topology_data, data.matrix_vector, child.GetRoot(), expected_matrix);

  // Ensure releasing one of the intermediate transforms does not clean up the matrix data since
  // it is still referenced in an active chain of Transforms.
  parent.ReleaseTransform(kId2);
  PRESENT(parent, true);

  data = ProcessMainLoop(parent.GetRoot());
  EXPECT_MATRIX(data.topology_data, data.matrix_vector, child.GetRoot(), expected_matrix);
}

TEST_F(FlatlandTest, GraphLinkReplaceWithoutConnection) {
  Flatland flatland = CreateFlatland();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<GraphLink> graph_link;
  flatland.LinkToParent(std::move(child_token), graph_link.NewRequest());

  ProcessMainLoop(flatland.GetRoot());
  PRESENT(flatland, true);
  ProcessMainLoop(flatland.GetRoot());

  ContentLinkToken parent_token2;
  GraphLinkToken child_token2;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token2.value, &child_token2.value));

  fidl::InterfacePtr<GraphLink> graph_link2;
  flatland.LinkToParent(std::move(child_token2), graph_link2.NewRequest());

  // Until Present() is called, the previous GraphLink is not unbound.
  EXPECT_TRUE(graph_link.is_bound());
  EXPECT_TRUE(graph_link2.is_bound());

  ProcessMainLoop(flatland.GetRoot());
  PRESENT(flatland, true);
  ProcessMainLoop(flatland.GetRoot());

  EXPECT_FALSE(graph_link.is_bound());
  EXPECT_TRUE(graph_link2.is_bound());
}

TEST_F(FlatlandTest, GraphLinkReplaceWithConnection) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  const ContentId kLinkId1 = 1;

  fidl::InterfacePtr<ContentLink> content_link;
  fidl::InterfacePtr<GraphLink> graph_link;
  CreateLink(&parent, &child, kLinkId1, &content_link, &graph_link);
  ProcessMainLoop(parent.GetRoot());

  fidl::InterfacePtr<GraphLink> graph_link2;

  // Don't use the helper function for the second link to test when the previous links are closed.
  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  // Creating the new GraphLink doesn't invalidate either of the old links until Present() is
  // called on the child.
  child.LinkToParent(std::move(child_token), graph_link2.NewRequest());

  EXPECT_TRUE(content_link.is_bound());
  EXPECT_TRUE(graph_link.is_bound());
  EXPECT_TRUE(graph_link2.is_bound());

  // Present() replaces the original GraphLink, which also results in the invalidation of both ends
  // of the original link.
  ProcessMainLoop(parent.GetRoot());
  PRESENT(child, true);
  ProcessMainLoop(parent.GetRoot());

  EXPECT_FALSE(content_link.is_bound());
  EXPECT_FALSE(graph_link.is_bound());
  EXPECT_TRUE(graph_link2.is_bound());
}

TEST_F(FlatlandTest, GraphLinkUnbindsOnParentDeath) {
  Flatland flatland = CreateFlatland();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<GraphLink> graph_link;
  flatland.LinkToParent(std::move(child_token), graph_link.NewRequest());

  ProcessMainLoop(flatland.GetRoot());
  PRESENT(flatland, true);
  ProcessMainLoop(flatland.GetRoot());

  parent_token.value.reset();
  ProcessMainLoop(flatland.GetRoot());

  EXPECT_FALSE(graph_link.is_bound());
}

TEST_F(FlatlandTest, GraphLinkUnbindsImmediatelyWithInvalidToken) {
  Flatland flatland = CreateFlatland();

  GraphLinkToken child_token;

  fidl::InterfacePtr<GraphLink> graph_link;
  flatland.LinkToParent(std::move(child_token), graph_link.NewRequest());

  // The link will be unbound even before Present() is called.
  RunLoopUntilIdle();
  EXPECT_FALSE(graph_link.is_bound());

  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, GraphUnlinkFailsWithoutLink) {
  Flatland flatland = CreateFlatland();

  flatland.UnlinkFromParent([](GraphLinkToken token) { EXPECT_TRUE(false); });

  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, GraphUnlinkReturnsOrphanedTokenOnParentDeath) {
  Flatland flatland = CreateFlatland();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<GraphLink> graph_link;
  flatland.LinkToParent(std::move(child_token), graph_link.NewRequest());

  RunLoopUntilIdle();
  PRESENT(flatland, true);
  RunLoopUntilIdle();

  // Killing the peer token does not prevent the instance from returning a valid token.
  parent_token.value.reset();
  RunLoopUntilIdle();

  GraphLinkToken graph_token;
  flatland.UnlinkFromParent(
      [&graph_token](GraphLinkToken token) { graph_token = std::move(token); });

  RunLoopUntilIdle();
  PRESENT(flatland, true);
  RunLoopUntilIdle();

  EXPECT_TRUE(graph_token.value.is_valid());

  // But trying to link with that token will immediately fail because it is already orphaned.
  fidl::InterfacePtr<GraphLink> graph_link2;
  flatland.LinkToParent(std::move(graph_token), graph_link2.NewRequest());

  RunLoopUntilIdle();
  PRESENT(flatland, true);
  RunLoopUntilIdle();

  EXPECT_FALSE(graph_link2.is_bound());
}

TEST_F(FlatlandTest, GraphUnlinkReturnsOriginalToken) {
  Flatland flatland = CreateFlatland();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  const zx_koid_t expected_koid = fsl::GetKoid(child_token.value.get());

  fidl::InterfacePtr<GraphLink> graph_link;
  flatland.LinkToParent(std::move(child_token), graph_link.NewRequest());

  RunLoopUntilIdle();
  PRESENT(flatland, true);
  RunLoopUntilIdle();

  GraphLinkToken graph_token;
  flatland.UnlinkFromParent(
      [&graph_token](GraphLinkToken token) { graph_token = std::move(token); });

  // Until Present() is called and the acquire fence is signaled, the previous GraphLink is not
  // unbound.
  EXPECT_TRUE(graph_link.is_bound());
  EXPECT_FALSE(graph_token.value.is_valid());

  PresentArgs args;
  args.acquire_fences = CreateEventArray(1);
  auto event_copy = CopyEvent(args.acquire_fences[0]);

  RunLoopUntilIdle();
  PRESENT_WITH_ARGS(flatland, std::move(args), true);
  RunLoopUntilIdle();

  EXPECT_TRUE(graph_link.is_bound());
  EXPECT_FALSE(graph_token.value.is_valid());

  // Signal the acquire fence to unbind the link.
  event_copy.signal(0, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();

  EXPECT_FALSE(graph_link.is_bound());
  EXPECT_TRUE(graph_token.value.is_valid());
  EXPECT_EQ(fsl::GetKoid(graph_token.value.get()), expected_koid);
}

TEST_F(FlatlandTest, ContentLinkUnbindsOnChildDeath) {
  Flatland flatland = CreateFlatland();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  const ContentId kLinkId1 = 1;

  fidl::InterfacePtr<ContentLink> content_link;
  LinkProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  flatland.CreateLink(kLinkId1, std::move(parent_token), std::move(properties),
                      content_link.NewRequest());

  ProcessMainLoop(flatland.GetRoot());
  PRESENT(flatland, true);
  ProcessMainLoop(flatland.GetRoot());

  child_token.value.reset();
  ProcessMainLoop(flatland.GetRoot());

  EXPECT_FALSE(content_link.is_bound());
}

TEST_F(FlatlandTest, ContentLinkUnbindsImmediatelyWithInvalidToken) {
  Flatland flatland = CreateFlatland();

  ContentLinkToken parent_token;

  const ContentId kLinkId1 = 1;

  fidl::InterfacePtr<ContentLink> content_link;
  flatland.CreateLink(kLinkId1, std::move(parent_token), {}, content_link.NewRequest());

  // The link will be unbound even before Present() is called.
  RunLoopUntilIdle();
  EXPECT_FALSE(content_link.is_bound());

  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, ContentLinkIdIsZero) {
  Flatland flatland = CreateFlatland();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<ContentLink> content_link;
  LinkProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  flatland.CreateLink(0, std::move(parent_token), std::move(properties), content_link.NewRequest());
  ProcessMainLoop(flatland.GetRoot());
  PRESENT(flatland, false);
  ProcessMainLoop(flatland.GetRoot());
}

TEST_F(FlatlandTest, ContentLinkNoLogicalSize) {
  Flatland flatland = CreateFlatland();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<ContentLink> content_link;
  LinkProperties properties;
  flatland.CreateLink(0, std::move(parent_token), std::move(properties), content_link.NewRequest());
  ProcessMainLoop(flatland.GetRoot());
  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, ContentLinkInvalidLogicalSize) {
  Flatland flatland = CreateFlatland();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<ContentLink> content_link;

  // The X value must be positive.
  LinkProperties properties;
  properties.set_logical_size({0.f, kDefaultSize});
  flatland.CreateLink(0, std::move(parent_token), std::move(properties), content_link.NewRequest());
  ProcessMainLoop(flatland.GetRoot());
  PRESENT(flatland, false);

  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  // The Y value must be positive.
  LinkProperties properties2;
  properties2.set_logical_size({kDefaultSize, 0.f});
  flatland.CreateLink(0, std::move(parent_token), std::move(properties2),
                      content_link.NewRequest());
  ProcessMainLoop(flatland.GetRoot());
  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, ContentLinkIdCollision) {
  Flatland flatland = CreateFlatland();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  const ContentId kId1 = 1;

  fidl::InterfacePtr<ContentLink> content_link;
  LinkProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  flatland.CreateLink(kId1, std::move(parent_token), std::move(properties),
                      content_link.NewRequest());
  ProcessMainLoop(flatland.GetRoot());
  PRESENT(flatland, true);
  ProcessMainLoop(flatland.GetRoot());

  ContentLinkToken parent_token2;
  GraphLinkToken child_token2;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token2.value, &child_token2.value));

  ProcessMainLoop(flatland.GetRoot());
  flatland.CreateLink(kId1, std::move(parent_token2), std::move(properties),
                      content_link.NewRequest());
  ProcessMainLoop(flatland.GetRoot());
  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, ClearGraphDelaysLinkDestructionUntilPresent) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  const ContentId kLinkId1 = 1;

  fidl::InterfacePtr<ContentLink> content_link;
  fidl::InterfacePtr<GraphLink> graph_link;
  CreateLink(&parent, &child, kLinkId1, &content_link, &graph_link);
  RunLoopUntilIdle();

  EXPECT_TRUE(content_link.is_bound());
  EXPECT_TRUE(graph_link.is_bound());

  // Clearing the parent graph should not unbind the interfaces until Present() is called and the
  // acquire fence is signaled.
  parent.ClearGraph();
  RunLoopUntilIdle();

  EXPECT_TRUE(content_link.is_bound());
  EXPECT_TRUE(graph_link.is_bound());

  PresentArgs args;
  args.acquire_fences = CreateEventArray(1);
  auto event_copy = CopyEvent(args.acquire_fences[0]);

  PRESENT_WITH_ARGS(parent, std::move(args), true);
  RunLoopUntilIdle();

  EXPECT_TRUE(content_link.is_bound());
  EXPECT_TRUE(graph_link.is_bound());

  // Signal the acquire fence to unbind the links.
  event_copy.signal(0, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();

  EXPECT_FALSE(content_link.is_bound());
  EXPECT_FALSE(graph_link.is_bound());

  // Recreate the Link. The parent graph was cleared so we can reuse the LinkId.
  CreateLink(&parent, &child, kLinkId1, &content_link, &graph_link);
  RunLoopUntilIdle();

  EXPECT_TRUE(content_link.is_bound());
  EXPECT_TRUE(graph_link.is_bound());

  // Clearing the child graph should not unbind the interfaces until Present() is called and the
  // acquire fence is signaled.
  child.ClearGraph();
  RunLoopUntilIdle();

  EXPECT_TRUE(content_link.is_bound());
  EXPECT_TRUE(graph_link.is_bound());

  PresentArgs args2;
  args2.acquire_fences = CreateEventArray(1);
  event_copy = CopyEvent(args2.acquire_fences[0]);

  PRESENT_WITH_ARGS(child, std::move(args2), true);
  RunLoopUntilIdle();

  EXPECT_TRUE(content_link.is_bound());
  EXPECT_TRUE(graph_link.is_bound());

  // Signal the acquire fence to unbind the links.
  event_copy.signal(0, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();

  EXPECT_FALSE(content_link.is_bound());
  EXPECT_FALSE(graph_link.is_bound());
}

// This test doesn't use the helper function to create a link, because it tests intermediate steps
// and timing corner cases.
TEST_F(FlatlandTest, ValidParentToChildFlow) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  const ContentId kLinkId = 1;

  fidl::InterfacePtr<ContentLink> content_link;
  LinkProperties properties;
  properties.set_logical_size({1.0f, 2.0f});
  parent.CreateLink(kLinkId, std::move(parent_token), std::move(properties),
                    content_link.NewRequest());

  fidl::InterfacePtr<GraphLink> graph_link;
  child.LinkToParent(std::move(child_token), graph_link.NewRequest());

  bool layout_updated = false;
  graph_link->GetLayout([&](LayoutInfo info) {
    EXPECT_EQ(1.0f, info.logical_size().x);
    EXPECT_EQ(2.0f, info.logical_size().y);
    layout_updated = true;
  });

  // Without even presenting, the child is able to get the initial properties from the parent.
  ProcessMainLoop(parent.GetRoot());
  EXPECT_TRUE(layout_updated);
}

// This test doesn't use the helper function to create a link, because it tests intermediate steps
// and timing corner cases.
TEST_F(FlatlandTest, ValidChildToParentFlow) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  const TransformId kTransformId = 1;
  const ContentId kLinkId = 2;

  parent.CreateTransform(kTransformId);
  parent.SetRootTransform(kTransformId);
  fidl::InterfacePtr<ContentLink> content_link;
  LinkProperties properties;
  properties.set_logical_size({1.0f, 2.0f});
  parent.CreateLink(kLinkId, std::move(parent_token), std::move(properties),
                    content_link.NewRequest());
  parent.SetContentOnTransform(kLinkId, kTransformId);

  fidl::InterfacePtr<GraphLink> graph_link;
  child.LinkToParent(std::move(child_token), graph_link.NewRequest());

  bool status_updated = false;
  content_link->GetStatus([&](ContentLinkStatus status) {
    ASSERT_EQ(ContentLinkStatus::CONTENT_HAS_PRESENTED, status);
    status_updated = true;
  });

  // The content link status cannot change until both parties have presented -- the parent Flatland
  // instance must Present() so that the graph is part of the global topology, and the child
  // Flatland instance must Present() so that CONTENT_HAS_PRESENTED can be true.
  EXPECT_FALSE(status_updated);
  PRESENT(parent, true);
  ProcessMainLoop(parent.GetRoot());
  PRESENT(child, true);
  EXPECT_FALSE(status_updated);
  ProcessMainLoop(parent.GetRoot());
  EXPECT_TRUE(status_updated);
}

TEST_F(FlatlandTest, LayoutOnlyUpdatesChildrenInGlobalTopology) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  const TransformId kTransformId = 1;
  const ContentId kLinkId = 2;

  fidl::InterfacePtr<ContentLink> content_link;
  fidl::InterfacePtr<GraphLink> graph_link;
  CreateLink(&parent, &child, kLinkId, &content_link, &graph_link);
  ProcessMainLoop(parent.GetRoot());

  // Confirm that the initial logical size is available immediately.
  {
    bool layout_updated = false;
    graph_link->GetLayout([&](LayoutInfo info) {
      EXPECT_EQ(kDefaultSize, info.logical_size().x);
      EXPECT_EQ(kDefaultSize, info.logical_size().y);
      layout_updated = true;
    });

    EXPECT_FALSE(layout_updated);
    ProcessMainLoop(parent.GetRoot());
    EXPECT_TRUE(layout_updated);
  }

  // Set the logical size to something new.
  {
    LinkProperties properties;
    properties.set_logical_size({2.0f, 3.0f});
    parent.SetLinkProperties(kLinkId, std::move(properties));
    PRESENT(parent, true);
  }

  // Confirm that no update is triggered since the child is not in the global topology.
  {
    bool layout_updated = false;
    graph_link->GetLayout([&](LayoutInfo info) { layout_updated = true; });

    EXPECT_FALSE(layout_updated);
    ProcessMainLoop(parent.GetRoot());
    EXPECT_FALSE(layout_updated);
  }

  // Attach the child to the global topology.
  parent.CreateTransform(kTransformId);
  parent.SetRootTransform(kTransformId);
  parent.SetContentOnTransform(kLinkId, kTransformId);
  PRESENT(parent, true);

  // Confirm that the new logical size is accessible.
  {
    bool layout_updated = false;
    graph_link->GetLayout([&](LayoutInfo info) {
      EXPECT_EQ(2.0f, info.logical_size().x);
      EXPECT_EQ(3.0f, info.logical_size().y);
      layout_updated = true;
    });

    EXPECT_FALSE(layout_updated);
    ProcessMainLoop(parent.GetRoot());
    EXPECT_TRUE(layout_updated);
  }
}

TEST_F(FlatlandTest, SetLinkPropertiesDefaultBehavior) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  const TransformId kTransformId = 1;
  const ContentId kLinkId = 2;

  fidl::InterfacePtr<ContentLink> content_link;
  fidl::InterfacePtr<GraphLink> graph_link;
  CreateLink(&parent, &child, kLinkId, &content_link, &graph_link);

  parent.CreateTransform(kTransformId);
  parent.SetRootTransform(kTransformId);
  parent.SetContentOnTransform(kLinkId, kTransformId);
  PRESENT(parent, true);

  ProcessMainLoop(parent.GetRoot());

  // Confirm that the initial layout is the default.
  {
    bool layout_updated = false;
    graph_link->GetLayout([&](LayoutInfo info) {
      EXPECT_EQ(kDefaultSize, info.logical_size().x);
      EXPECT_EQ(kDefaultSize, info.logical_size().y);
      layout_updated = true;
    });

    EXPECT_FALSE(layout_updated);
    ProcessMainLoop(parent.GetRoot());
    EXPECT_TRUE(layout_updated);
  }

  // Set the logical size to something new.
  {
    LinkProperties properties;
    properties.set_logical_size({2.0f, 3.0f});
    parent.SetLinkProperties(kLinkId, std::move(properties));
    PRESENT(parent, true);
  }

  // Confirm that the new logical size is accessible.
  {
    bool layout_updated = false;
    graph_link->GetLayout([&](LayoutInfo info) {
      EXPECT_EQ(2.0f, info.logical_size().x);
      EXPECT_EQ(3.0f, info.logical_size().y);
      layout_updated = true;
    });

    EXPECT_FALSE(layout_updated);
    ProcessMainLoop(parent.GetRoot());
    EXPECT_TRUE(layout_updated);
  }

  // Set link properties using a properties object with an unset size field.
  {
    LinkProperties default_properties;
    parent.SetLinkProperties(kLinkId, std::move(default_properties));
    PRESENT(parent, true);
  }

  // Confirm that no update has been triggered.
  {
    bool layout_updated = false;
    graph_link->GetLayout([&](LayoutInfo info) { layout_updated = true; });

    EXPECT_FALSE(layout_updated);
    ProcessMainLoop(parent.GetRoot());
    EXPECT_FALSE(layout_updated);
  }
}

TEST_F(FlatlandTest, SetLinkPropertiesMultisetBehavior) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  const TransformId kTransformId = 1;
  const ContentId kLinkId = 2;

  fidl::InterfacePtr<ContentLink> content_link;
  fidl::InterfacePtr<GraphLink> graph_link;
  CreateLink(&parent, &child, kLinkId, &content_link, &graph_link);

  // Our initial layout (from link creation) should be the default size.
  {
    int num_updates = 0;
    graph_link->GetLayout([&](LayoutInfo info) {
      EXPECT_EQ(kDefaultSize, info.logical_size().x);
      EXPECT_EQ(kDefaultSize, info.logical_size().y);
      ++num_updates;
    });

    EXPECT_EQ(0, num_updates);
    ProcessMainLoop(parent.GetRoot());
    EXPECT_EQ(1, num_updates);
  }

  // Create a full chain of transforms from parent root to child root.
  parent.CreateTransform(kTransformId);
  parent.SetRootTransform(kTransformId);
  parent.SetContentOnTransform(kLinkId, kTransformId);
  PRESENT(parent, true);

  const float kInitialSize = 100.0f;

  // Set the logical size to something new multiple times.
  for (int i = 10; i >= 0; --i) {
    LinkProperties properties;
    properties.set_logical_size({kInitialSize + i + 1.0f, kInitialSize + i + 1.0f});
    parent.SetLinkProperties(kLinkId, std::move(properties));
    LinkProperties properties2;
    properties2.set_logical_size({kInitialSize + i, kInitialSize + i});
    parent.SetLinkProperties(kLinkId, std::move(properties2));
    PRESENT(parent, true);
  }

  // Confirm that the callback is fired once, and that it has the most up-to-date data.
  {
    int num_updates = 0;
    graph_link->GetLayout([&](LayoutInfo info) {
      EXPECT_EQ(kInitialSize, info.logical_size().x);
      EXPECT_EQ(kInitialSize, info.logical_size().y);
      ++num_updates;
    });

    EXPECT_EQ(0, num_updates);
    ProcessMainLoop(parent.GetRoot());
    EXPECT_EQ(1, num_updates);
  }

  const float kNewSize = 50.0f;

  // Confirm that calling GetLayout again results in a hung get.
  int num_updates = 0;
  graph_link->GetLayout([&](LayoutInfo info) {
    // When we receive the new layout information, confirm that we receive the last update in the
    // batch.
    EXPECT_EQ(kNewSize, info.logical_size().x);
    EXPECT_EQ(kNewSize, info.logical_size().y);
    ++num_updates;
  });

  EXPECT_EQ(0, num_updates);
  ProcessMainLoop(parent.GetRoot());
  EXPECT_EQ(0, num_updates);

  // Update the properties twice, once with the old value, once with the new value.
  {
    LinkProperties properties;
    properties.set_logical_size({kInitialSize, kInitialSize});
    parent.SetLinkProperties(kLinkId, std::move(properties));
    LinkProperties properties2;
    properties2.set_logical_size({kNewSize, kNewSize});
    parent.SetLinkProperties(kLinkId, std::move(properties2));
    PRESENT(parent, true);
  }

  // Confirm that we receive the update.
  EXPECT_EQ(0, num_updates);
  ProcessMainLoop(parent.GetRoot());
  EXPECT_EQ(1, num_updates);
}

TEST_F(FlatlandTest, SetLinkPropertiesOnMultipleChildren) {
  const int kNumChildren = 3;
  const TransformId kRootTransform = 1;
  const TransformId kTransformIds[kNumChildren] = {2, 3, 4};
  const ContentId kLinkIds[kNumChildren] = {5, 6, 7};

  Flatland parent = CreateFlatland();
  Flatland children[kNumChildren] = {CreateFlatland(), CreateFlatland(), CreateFlatland()};
  fidl::InterfacePtr<ContentLink> content_link[kNumChildren];
  fidl::InterfacePtr<GraphLink> graph_link[kNumChildren];

  parent.CreateTransform(kRootTransform);
  parent.SetRootTransform(kRootTransform);

  for (int i = 0; i < kNumChildren; ++i) {
    parent.CreateTransform(kTransformIds[i]);
    parent.AddChild(kRootTransform, kTransformIds[i]);
    CreateLink(&parent, &children[i], kLinkIds[i], &content_link[i], &graph_link[i]);
    parent.SetContentOnTransform(kLinkIds[i], kTransformIds[i]);
  }
  ProcessMainLoop(parent.GetRoot());

  const float kDefaultSize = 1.0f;

  // Confirm that all children are at the default value
  for (int i = 0; i < kNumChildren; ++i) {
    bool layout_updated = false;
    graph_link[i]->GetLayout([&](LayoutInfo info) {
      EXPECT_EQ(kDefaultSize, info.logical_size().x);
      EXPECT_EQ(kDefaultSize, info.logical_size().y);
      layout_updated = true;
    });

    EXPECT_FALSE(layout_updated);
    ProcessMainLoop(parent.GetRoot());
    EXPECT_TRUE(layout_updated);
  }

  // Resize the content on all children.
  for (auto id : kLinkIds) {
    LinkProperties properties;
    properties.set_logical_size({static_cast<float>(id), id * 2.0f});
    parent.SetLinkProperties(id, std::move(properties));
  }

  PRESENT(parent, true);

  for (int i = 0; i < kNumChildren; ++i) {
    bool layout_updated = false;
    graph_link[i]->GetLayout([&](LayoutInfo info) {
      EXPECT_EQ(kLinkIds[i], info.logical_size().x);
      EXPECT_EQ(kLinkIds[i] * 2.0f, info.logical_size().y);
      layout_updated = true;
    });

    EXPECT_FALSE(layout_updated);
    ProcessMainLoop(parent.GetRoot());
    EXPECT_TRUE(layout_updated);
  }
}

TEST_F(FlatlandTest, DisplayPixelScaleAffectsPixelScale) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  const TransformId kTransformId = 1;
  const ContentId kLinkId = 2;

  fidl::InterfacePtr<ContentLink> content_link;
  fidl::InterfacePtr<GraphLink> graph_link;
  CreateLink(&parent, &child, kLinkId, &content_link, &graph_link);

  parent.CreateTransform(kTransformId);
  parent.SetRootTransform(kTransformId);
  parent.SetContentOnTransform(kLinkId, kTransformId);
  PRESENT(parent, true);

  ProcessMainLoop(parent.GetRoot());

  // Change the display pixel scale.
  const glm::vec2 new_display_pixel_scale = {0.1f, 0.2f};
  SetDisplayPixelScale(new_display_pixel_scale);

  // Call and ignore GetLayout() to guarantee the next call hangs.
  graph_link->GetLayout([&](LayoutInfo info) {});

  // Confirm that the new pixel scale is (.1, .2).
  {
    bool layout_updated = false;
    graph_link->GetLayout([&](LayoutInfo info) {
      EXPECT_EQ(new_display_pixel_scale.x, info.pixel_scale().x);
      EXPECT_EQ(new_display_pixel_scale.y, info.pixel_scale().y);
      layout_updated = true;
    });

    EXPECT_FALSE(layout_updated);
    ProcessMainLoop(parent.GetRoot());
    EXPECT_TRUE(layout_updated);
  }
}

TEST_F(FlatlandTest, LinkSizesAffectPixelScale) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  const TransformId kTransformId = 1;
  const ContentId kLinkId = 2;

  fidl::InterfacePtr<ContentLink> content_link;
  fidl::InterfacePtr<GraphLink> graph_link;
  CreateLink(&parent, &child, kLinkId, &content_link, &graph_link);

  parent.CreateTransform(kTransformId);
  parent.SetRootTransform(kTransformId);
  parent.SetContentOnTransform(kLinkId, kTransformId);
  PRESENT(parent, true);

  ProcessMainLoop(parent.GetRoot());

  // Change the link size and logical size of the link.
  const Vec2 kNewLinkSize = {2.f, 3.f};
  parent.SetLinkSize(kLinkId, kNewLinkSize);

  const Vec2 kNewLogicalSize = {5.f, 7.f};
  {
    LinkProperties properties;
    properties.set_logical_size(kNewLogicalSize);
    parent.SetLinkProperties(kLinkId, std::move(properties));
  }

  PRESENT(parent, true);

  // Call and ignore GetLayout() to guarantee the next call hangs.
  graph_link->GetLayout([&](LayoutInfo info) {});

  // Confirm that the new pixel scale is (2 / 5, 3 / 7).
  {
    bool layout_updated = false;
    graph_link->GetLayout([&](LayoutInfo info) {
      EXPECT_FLOAT_EQ(kNewLinkSize.x / kNewLogicalSize.x, info.pixel_scale().x);
      EXPECT_FLOAT_EQ(kNewLinkSize.y / kNewLogicalSize.y, info.pixel_scale().y);
      layout_updated = true;
    });

    EXPECT_FALSE(layout_updated);
    ProcessMainLoop(parent.GetRoot());
    EXPECT_TRUE(layout_updated);
  }
}

TEST_F(FlatlandTest, GeometricAttributesAffectPixelScale) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  const TransformId kTransformId = 1;
  const ContentId kLinkId = 2;

  fidl::InterfacePtr<ContentLink> content_link;
  fidl::InterfacePtr<GraphLink> graph_link;
  CreateLink(&parent, &child, kLinkId, &content_link, &graph_link);

  parent.CreateTransform(kTransformId);
  parent.SetRootTransform(kTransformId);
  parent.SetContentOnTransform(kLinkId, kTransformId);
  PRESENT(parent, true);

  ProcessMainLoop(parent.GetRoot());

  // Set a scale on the parent transform.
  const Vec2 scale = {2.f, 3.f};
  parent.SetScale(kTransformId, scale);
  PRESENT(parent, true);

  // Call and ignore GetLayout() to guarantee the next call hangs.
  graph_link->GetLayout([&](LayoutInfo info) {});

  // Confirm that the new pixel scale is (2, 3).
  {
    bool layout_updated = false;
    graph_link->GetLayout([&](LayoutInfo info) {
      EXPECT_FLOAT_EQ(scale.x, info.pixel_scale().x);
      EXPECT_FLOAT_EQ(scale.y, info.pixel_scale().y);
      layout_updated = true;
    });

    EXPECT_FALSE(layout_updated);
    ProcessMainLoop(parent.GetRoot());
    EXPECT_TRUE(layout_updated);
  }

  // Set a negative scale, but confirm that pixel scale is still positive.
  parent.SetScale(kTransformId, {-scale.x, -scale.y});
  PRESENT(parent, true);

  // Call and ignore GetLayout() to guarantee the next call hangs.
  graph_link->GetLayout([&](LayoutInfo info) {});

  // Pixel scale is still (2, 3), so nothing changes.
  {
    bool layout_updated = false;
    graph_link->GetLayout([&](LayoutInfo info) { layout_updated = true; });

    EXPECT_FALSE(layout_updated);
    ProcessMainLoop(parent.GetRoot());
    EXPECT_FALSE(layout_updated);
  }

  // Set a rotation on the parent transform.
  parent.SetOrientation(kTransformId, Orientation::CCW_90_DEGREES);
  PRESENT(parent, true);

  // Call and ignore GetLayout() to guarantee the next call hangs.
  graph_link->GetLayout([&](LayoutInfo info) {});

  // Confirm that this flips the new pixel scale to (3, 2).
  {
    bool layout_updated = false;
    graph_link->GetLayout([&](LayoutInfo info) {
      EXPECT_FLOAT_EQ(scale.y, info.pixel_scale().x);
      EXPECT_FLOAT_EQ(scale.x, info.pixel_scale().y);
      layout_updated = true;
    });

    EXPECT_FALSE(layout_updated);
    ProcessMainLoop(parent.GetRoot());
    EXPECT_TRUE(layout_updated);
  }
}

TEST_F(FlatlandTest, SetLinkOnTransformErrorCases) {
  Flatland flatland = CreateFlatland();

  // Setup.

  const TransformId kId1 = 1;
  const TransformId kId2 = 2;

  flatland.CreateTransform(kId1);

  const ContentId kLinkId1 = 1;
  const ContentId kLinkId2 = 2;

  fidl::InterfacePtr<ContentLink> content_link;

  // Creating a link with an empty property object is an error. Logical size must be provided at
  // creation time.
  {
    ContentLinkToken parent_token;
    GraphLinkToken child_token;
    ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));
    LinkProperties empty_properties;
    flatland.CreateLink(kLinkId1, std::move(parent_token), std::move(empty_properties),
                        content_link.NewRequest());

    PRESENT(flatland, false);
  }

  // We have to recreate our tokens to get a valid link object.
  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  LinkProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  flatland.CreateLink(kLinkId1, std::move(parent_token), std::move(properties),
                      content_link.NewRequest());

  PRESENT(flatland, true);

  // Zero is not a valid transform_id.
  flatland.SetContentOnTransform(kLinkId1, 0);
  PRESENT(flatland, false);

  // Setting a valid link on an ivnalid transform is not valid.
  flatland.SetContentOnTransform(kLinkId1, kId2);
  PRESENT(flatland, false);

  // Setting an invalid link on a valid transform is not valid.
  flatland.SetContentOnTransform(kLinkId2, kId1);
  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, ReleaseLinkErrorCases) {
  Flatland flatland = CreateFlatland();

  // Zero is not a valid link_id.
  flatland.ReleaseLink(0, [](ContentLinkToken token) { EXPECT_TRUE(false); });
  PRESENT(flatland, false);

  // Using a link_id that does not exist is not valid.
  const ContentId kLinkId1 = 1;
  flatland.ReleaseLink(kLinkId1, [](ContentLinkToken token) { EXPECT_TRUE(false); });
  PRESENT(flatland, false);

  // ContentId is not a Link.
  const ContentId kImageId = 2;
  const BufferCollectionId kBufferCollectionId = 3;

  ImageProperties properties;
  properties.set_width(100);
  properties.set_height(200);

  CreateImage(&flatland, kImageId, kBufferCollectionId, std::move(properties));

  flatland.ReleaseLink(kImageId, [](ContentLinkToken token) { EXPECT_TRUE(false); });
  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, ReleaseLinkReturnsOriginalToken) {
  Flatland flatland = CreateFlatland();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  const zx_koid_t expected_koid = fsl::GetKoid(parent_token.value.get());

  const ContentId kLinkId1 = 1;

  fidl::InterfacePtr<ContentLink> content_link;
  LinkProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  flatland.CreateLink(kLinkId1, std::move(parent_token), std::move(properties),
                      content_link.NewRequest());

  RunLoopUntilIdle();
  PRESENT(flatland, true);
  RunLoopUntilIdle();

  ContentLinkToken content_token;
  flatland.ReleaseLink(
      kLinkId1, [&content_token](ContentLinkToken token) { content_token = std::move(token); });

  // Until Present() is called and the acquire fence is signaled, the previous ContentLink is not
  // unbound.
  EXPECT_TRUE(content_link.is_bound());
  EXPECT_FALSE(content_token.value.is_valid());

  PresentArgs args;
  args.acquire_fences = CreateEventArray(1);
  auto event_copy = CopyEvent(args.acquire_fences[0]);

  RunLoopUntilIdle();
  PRESENT_WITH_ARGS(flatland, std::move(args), true);
  RunLoopUntilIdle();

  EXPECT_TRUE(content_link.is_bound());
  EXPECT_FALSE(content_token.value.is_valid());

  // Signal the acquire fence to unbind the link.
  event_copy.signal(0, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();

  EXPECT_FALSE(content_link.is_bound());
  EXPECT_TRUE(content_token.value.is_valid());
  EXPECT_EQ(fsl::GetKoid(content_token.value.get()), expected_koid);
}

TEST_F(FlatlandTest, ReleaseLinkReturnsOrphanedTokenOnChildDeath) {
  Flatland flatland = CreateFlatland();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  const ContentId kLinkId1 = 1;

  fidl::InterfacePtr<ContentLink> content_link;
  LinkProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  flatland.CreateLink(kLinkId1, std::move(parent_token), std::move(properties),
                      content_link.NewRequest());

  RunLoopUntilIdle();
  PRESENT(flatland, true);
  RunLoopUntilIdle();

  // Killing the peer token does not prevent the instance from returning a valid token.
  child_token.value.reset();
  RunLoopUntilIdle();

  ContentLinkToken content_token;
  flatland.ReleaseLink(
      kLinkId1, [&content_token](ContentLinkToken token) { content_token = std::move(token); });

  RunLoopUntilIdle();
  PRESENT(flatland, true);
  RunLoopUntilIdle();

  EXPECT_TRUE(content_token.value.is_valid());

  // But trying to link with that token will immediately fail because it is already orphaned.
  const ContentId kLinkId2 = 2;

  fidl::InterfacePtr<ContentLink> content_link2;
  flatland.CreateLink(kLinkId2, std::move(content_token), std::move(properties),
                      content_link2.NewRequest());

  RunLoopUntilIdle();
  PRESENT(flatland, true);
  RunLoopUntilIdle();

  EXPECT_FALSE(content_link2.is_bound());
}

TEST_F(FlatlandTest, CreateLinkPresentedBeforeLinkToParent) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  // Create a transform, add it to the parent, then create a link and assign to the transform.
  const TransformId kId1 = 1;
  parent.CreateTransform(kId1);
  parent.SetRootTransform(kId1);

  const ContentId kLinkId = 1;

  fidl::InterfacePtr<ContentLink> parent_content_link;
  LinkProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  parent.CreateLink(kLinkId, std::move(parent_token), std::move(properties),
                    parent_content_link.NewRequest());
  parent.SetContentOnTransform(kLinkId, kId1);

  PRESENT(parent, true);
  RunLoopUntilIdle();

  // Link the child to the parent.
  fidl::InterfacePtr<GraphLink> child_graph_link;
  child.LinkToParent(std::move(child_token), child_graph_link.NewRequest());

  // The child should only be accessible from the parent when Present() is called on the child.
  EXPECT_FALSE(IsDescendantOf(parent.GetRoot(), child.GetRoot()));

  PRESENT(child, true);
  RunLoopUntilIdle();

  EXPECT_TRUE(IsDescendantOf(parent.GetRoot(), child.GetRoot()));
}

TEST_F(FlatlandTest, LinkToParentPresentedBeforeCreateLink) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  // Link the child to the parent
  fidl::InterfacePtr<GraphLink> child_graph_link;
  child.LinkToParent(std::move(child_token), child_graph_link.NewRequest());

  PRESENT(child, true);

  // Create a transform, add it to the parent, then create a link and assign to the transform.
  const TransformId kId1 = 1;
  parent.CreateTransform(kId1);
  parent.SetRootTransform(kId1);

  // Present the parent once so that it has a topology or else IsDescendantOf() will crash.
  PRESENT(parent, true);
  RunLoopUntilIdle();

  const ContentId kLinkId = 1;

  fidl::InterfacePtr<ContentLink> parent_content_link;
  LinkProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  parent.CreateLink(kLinkId, std::move(parent_token), std::move(properties),
                    parent_content_link.NewRequest());
  parent.SetContentOnTransform(kLinkId, kId1);

  // The child should only be accessible from the parent when Present() is called on the parent.
  EXPECT_FALSE(IsDescendantOf(parent.GetRoot(), child.GetRoot()));

  PRESENT(parent, true);
  RunLoopUntilIdle();

  EXPECT_TRUE(IsDescendantOf(parent.GetRoot(), child.GetRoot()));
}

TEST_F(FlatlandTest, LinkResolvedBeforeEitherPresent) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  // Create a transform, add it to the parent, then create a link and assign to the transform.
  const TransformId kId1 = 1;
  parent.CreateTransform(kId1);
  parent.SetRootTransform(kId1);

  // Present the parent once so that it has a topology or else IsDescendantOf() will crash.
  PRESENT(parent, true);
  RunLoopUntilIdle();

  const ContentId kLinkId = 1;

  fidl::InterfacePtr<ContentLink> parent_content_link;
  LinkProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  parent.CreateLink(kLinkId, std::move(parent_token), std::move(properties),
                    parent_content_link.NewRequest());
  parent.SetContentOnTransform(kLinkId, kId1);

  // Link the child to the parent.
  fidl::InterfacePtr<GraphLink> child_graph_link;
  child.LinkToParent(std::move(child_token), child_graph_link.NewRequest());

  // The child should only be accessible from the parent when Present() is called on both the parent
  // and the child.
  EXPECT_FALSE(IsDescendantOf(parent.GetRoot(), child.GetRoot()));

  PRESENT(parent, true);
  RunLoopUntilIdle();

  EXPECT_FALSE(IsDescendantOf(parent.GetRoot(), child.GetRoot()));

  PRESENT(child, true);
  RunLoopUntilIdle();

  EXPECT_TRUE(IsDescendantOf(parent.GetRoot(), child.GetRoot()));
}

TEST_F(FlatlandTest, ClearChildLink) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  // Create and link the two instances.
  const TransformId kId1 = 1;
  parent.CreateTransform(kId1);
  parent.SetRootTransform(kId1);

  const ContentId kLinkId = 1;

  fidl::InterfacePtr<ContentLink> parent_content_link;
  LinkProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  parent.CreateLink(kLinkId, std::move(parent_token), std::move(properties),
                    parent_content_link.NewRequest());
  parent.SetContentOnTransform(kLinkId, kId1);

  fidl::InterfacePtr<GraphLink> child_graph_link;
  child.LinkToParent(std::move(child_token), child_graph_link.NewRequest());

  PRESENT(parent, true);
  PRESENT(child, true);
  RunLoopUntilIdle();

  EXPECT_TRUE(IsDescendantOf(parent.GetRoot(), child.GetRoot()));

  // Reset the child link using zero as the link id.
  parent.SetContentOnTransform(0, kId1);

  PRESENT(parent, true);
  RunLoopUntilIdle();

  EXPECT_FALSE(IsDescendantOf(parent.GetRoot(), child.GetRoot()));
}

TEST_F(FlatlandTest, RelinkUnlinkedParentSameToken) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  const ContentId kLinkId1 = 1;

  fidl::InterfacePtr<ContentLink> content_link;
  fidl::InterfacePtr<GraphLink> graph_link;
  CreateLink(&parent, &child, kLinkId1, &content_link, &graph_link);
  RunLoopUntilIdle();

  const TransformId kId1 = 1;
  parent.CreateTransform(kId1);
  parent.SetRootTransform(kId1);
  parent.SetContentOnTransform(kId1, kLinkId1);

  PRESENT(parent, true);
  RunLoopUntilIdle();

  EXPECT_TRUE(IsDescendantOf(parent.GetRoot(), child.GetRoot()));

  GraphLinkToken graph_token;
  child.UnlinkFromParent([&graph_token](GraphLinkToken token) { graph_token = std::move(token); });

  PRESENT(child, true);
  RunLoopUntilIdle();

  EXPECT_FALSE(IsDescendantOf(parent.GetRoot(), child.GetRoot()));

  // The same token can be used to link a different instance.
  Flatland child2 = CreateFlatland();
  child2.LinkToParent(std::move(graph_token), graph_link.NewRequest());

  PRESENT(child2, true);
  RunLoopUntilIdle();

  EXPECT_TRUE(IsDescendantOf(parent.GetRoot(), child2.GetRoot()));

  // The old instance is not re-linked.
  EXPECT_FALSE(IsDescendantOf(parent.GetRoot(), child.GetRoot()));
}

TEST_F(FlatlandTest, RecreateReleasedLinkSameToken) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  const ContentId kLinkId1 = 1;

  fidl::InterfacePtr<ContentLink> content_link;
  fidl::InterfacePtr<GraphLink> graph_link;
  CreateLink(&parent, &child, kLinkId1, &content_link, &graph_link);
  RunLoopUntilIdle();

  const TransformId kId1 = 1;
  parent.CreateTransform(kId1);
  parent.SetRootTransform(kId1);
  parent.SetContentOnTransform(kId1, kLinkId1);

  PRESENT(parent, true);
  RunLoopUntilIdle();

  EXPECT_TRUE(IsDescendantOf(parent.GetRoot(), child.GetRoot()));

  ContentLinkToken content_token;
  parent.ReleaseLink(
      kLinkId1, [&content_token](ContentLinkToken token) { content_token = std::move(token); });

  PRESENT(parent, true);
  RunLoopUntilIdle();

  EXPECT_FALSE(IsDescendantOf(parent.GetRoot(), child.GetRoot()));

  // The same token can be used to create a different link to the same child with a different
  // parent.
  Flatland parent2 = CreateFlatland();

  const TransformId kId2 = 2;
  parent2.CreateTransform(kId2);
  parent2.SetRootTransform(kId2);

  const ContentId kLinkId2 = 2;
  LinkProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  parent2.CreateLink(kLinkId2, std::move(content_token), std::move(properties),
                     content_link.NewRequest());
  parent2.SetContentOnTransform(kId2, kLinkId2);

  PRESENT(parent2, true);
  RunLoopUntilIdle();

  EXPECT_TRUE(IsDescendantOf(parent2.GetRoot(), child.GetRoot()));

  // The old instance is not re-linked.
  EXPECT_FALSE(IsDescendantOf(parent.GetRoot(), child.GetRoot()));
}

TEST_F(FlatlandTest, SetLinkSizeErrorCases) {
  Flatland flatland = CreateFlatland();

  const ContentId kIdNotCreated = 1;

  // Zero is not a valid transform ID.
  flatland.SetLinkSize(0, {1.f, 2.f});
  PRESENT(flatland, false);

  // Size contains non-positive components.
  flatland.SetLinkSize(0, {-1.f, 2.f});
  PRESENT(flatland, false);

  flatland.SetLinkSize(0, {1.f, 0.f});
  PRESENT(flatland, false);

  // Link does not exist.
  flatland.SetLinkSize(kIdNotCreated, {1.f, 2.f});
  PRESENT(flatland, false);

  // ContentId is not a Link.
  const ContentId kImageId = 2;
  const BufferCollectionId kBufferCollectionId = 3;

  ImageProperties properties;
  properties.set_width(100);
  properties.set_height(200);

  CreateImage(&flatland, kImageId, kBufferCollectionId, std::move(properties));

  flatland.SetLinkSize(kImageId, {1.f, 2.f});
  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, LinkSizeRatiosCreateScaleMatrix) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  const ContentId kLinkId1 = 1;

  fidl::InterfacePtr<ContentLink> content_link;
  fidl::InterfacePtr<GraphLink> graph_link;
  CreateLink(&parent, &child, kLinkId1, &content_link, &graph_link);
  RunLoopUntilIdle();

  const TransformId kId1 = 1;

  parent.CreateTransform(kId1);
  parent.SetRootTransform(kId1);
  parent.SetContentOnTransform(kLinkId1, kId1);

  PRESENT(parent, true);

  // The default size is the same as the logical size, so the child root has an identity matrix.
  // With no properties set, the child root has an identity transform.
  auto data = ProcessMainLoop(parent.GetRoot());
  EXPECT_MATRIX(data.topology_data, data.matrix_vector, child.GetRoot(), glm::mat3());

  // Change the link size to half the width and a quarter the height.
  const float kNewLinkWidth = 0.5f * kDefaultSize;
  const float kNewLinkHeight = 0.25f * kDefaultSize;
  parent.SetLinkSize(kLinkId1, {kNewLinkWidth, kNewLinkHeight});

  PRESENT(parent, true);

  // This should change the expected matrix to apply the same scales.
  const glm::mat3 expected_scale_matrix = glm::scale(glm::mat3(), {kNewLinkWidth, kNewLinkHeight});

  data = ProcessMainLoop(parent.GetRoot());
  EXPECT_MATRIX(data.topology_data, data.matrix_vector, child.GetRoot(), expected_scale_matrix);

  // Changing the logical size to the same values returns the matrix to the identity matrix.
  LinkProperties properties;
  properties.set_logical_size({kNewLinkWidth, kNewLinkHeight});
  parent.SetLinkProperties(kLinkId1, std::move(properties));

  PRESENT(parent, true);

  data = ProcessMainLoop(parent.GetRoot());
  EXPECT_MATRIX(data.topology_data, data.matrix_vector, child.GetRoot(), glm::mat3());

  // Change the logical size back to the default size.
  LinkProperties properties2;
  properties2.set_logical_size({kDefaultSize, kDefaultSize});
  parent.SetLinkProperties(kLinkId1, std::move(properties2));

  PRESENT(parent, true);

  // This should change the expected matrix back to applying the scales.
  data = ProcessMainLoop(parent.GetRoot());
  EXPECT_MATRIX(data.topology_data, data.matrix_vector, child.GetRoot(), expected_scale_matrix);
}

TEST_F(FlatlandTest, EmptyLogicalSizePreservesOldSize) {
  Flatland parent = CreateFlatland();
  Flatland child = CreateFlatland();

  const ContentId kLinkId1 = 1;

  fidl::InterfacePtr<ContentLink> content_link;
  fidl::InterfacePtr<GraphLink> graph_link;
  CreateLink(&parent, &child, kLinkId1, &content_link, &graph_link);
  RunLoopUntilIdle();

  const TransformId kId1 = 1;

  parent.CreateTransform(kId1);
  parent.SetRootTransform(kId1);
  parent.SetContentOnTransform(kLinkId1, kId1);

  PRESENT(parent, true);

  // Set the link size and logical size to new values
  const float kNewLinkWidth = 2.f * kDefaultSize;
  const float kNewLinkHeight = 3.f * kDefaultSize;
  parent.SetLinkSize(kLinkId1, {kNewLinkWidth, kNewLinkHeight});

  const float kNewLinkLogicalWidth = 5.f * kDefaultSize;
  const float kNewLinkLogicalHeight = 7.f * kDefaultSize;
  LinkProperties properties;
  properties.set_logical_size({kNewLinkLogicalWidth, kNewLinkLogicalHeight});
  parent.SetLinkProperties(kLinkId1, std::move(properties));

  PRESENT(parent, true);

  // This should result in an expected matrix that applies the ratio of the scales.
  glm::mat3 expected_scale_matrix = glm::scale(
      glm::mat3(), {kNewLinkWidth / kNewLinkLogicalWidth, kNewLinkHeight / kNewLinkLogicalHeight});

  auto data = ProcessMainLoop(parent.GetRoot());
  EXPECT_MATRIX(data.topology_data, data.matrix_vector, child.GetRoot(), expected_scale_matrix);

  // Setting a new LinkProperties with no logical size shouldn't change the matrix.
  LinkProperties properties2;
  parent.SetLinkProperties(kLinkId1, std::move(properties2));

  PRESENT(parent, true);

  data = ProcessMainLoop(parent.GetRoot());
  EXPECT_MATRIX(data.topology_data, data.matrix_vector, child.GetRoot(), expected_scale_matrix);

  // But it should still preserve the old logical size so that a subsequent link size update uses
  // the old logical size.
  const float kNewLinkWidth2 = 11.f * kDefaultSize;
  const float kNewLinkHeight2 = 13.f * kDefaultSize;
  parent.SetLinkSize(kLinkId1, {kNewLinkWidth2, kNewLinkHeight2});

  PRESENT(parent, true);

  // This should result in an expected matrix that applies the ratio of the scales.
  expected_scale_matrix = glm::scale(glm::mat3(), {kNewLinkWidth2 / kNewLinkLogicalWidth,
                                                   kNewLinkHeight2 / kNewLinkLogicalHeight});

  data = ProcessMainLoop(parent.GetRoot());
  EXPECT_MATRIX(data.topology_data, data.matrix_vector, child.GetRoot(), expected_scale_matrix);
}

TEST_F(FlatlandTest, RegisterBufferCollectionErrorCases) {
  Flatland flatland = CreateFlatland();

  // Zero is not a valid buffer collection ID.
  {
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
    flatland.RegisterBufferCollection(0, std::move(token));
    PRESENT(flatland, false);
  }

  // The Renderer registration call can fail.
  {
    // Mock the Renderer call to fail.
    EXPECT_CALL(*mock_renderer_, RegisterTextureCollection(_, _))
        .WillOnce(Return(Renderer::kInvalidId));

    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
    flatland.RegisterBufferCollection(1, std::move(token));
    PRESENT(flatland, false);
  }

  // Two buffer collections cannot use the same ID.
  {
    const BufferCollectionId kId = 1;

    EXPECT_CALL(*mock_renderer_, RegisterTextureCollection(_, _)).WillOnce(Return(1));

    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
    flatland.RegisterBufferCollection(kId, std::move(token));
    PRESENT(flatland, true);

    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token2;
    flatland.RegisterBufferCollection(kId, std::move(token2));
    PRESENT(flatland, false);
  }
}

// Tests that Flatland passes the Sysmem token to the Renderer even if the client has not called
// Present(). This is necessary since the client may block on buffers being allocated before
// presenting.
TEST_F(FlatlandTest, RendererGetsSysmemTokenBeforePresent) {
  Flatland flatland = CreateFlatland();

  // Register a buffer collection and expect the mock Renderer call, even without presenting.
  const BufferCollectionId kId = 1;
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;

  EXPECT_CALL(*mock_renderer_, RegisterTextureCollection(_, _)).WillOnce(Return(1));
  flatland.RegisterBufferCollection(kId, std::move(token));
}

TEST_F(FlatlandTest, CreateImageErrorCases) {
  Flatland flatland = CreateFlatland();

  // Default image properties.
  const uint32_t kDefaultVmoIndex = 1;
  const uint32_t kDefaultWidth = 100;
  const uint32_t kDefaultHeight = 1000;

  // Setup a valid buffer collection.
  const BufferCollectionId kBufferCollectionId = 1;
  const GlobalBufferCollectionId kGlobalBufferCollectionId = 2;

  EXPECT_CALL(*mock_renderer_, RegisterTextureCollection(_, _))
      .WillOnce(Return(kGlobalBufferCollectionId));

  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
  flatland.RegisterBufferCollection(kBufferCollectionId, std::move(token));
  PRESENT(flatland, true);

  // Zero is not a valid image ID.
  {
    ImageProperties properties;
    flatland.CreateImage(0, kBufferCollectionId, kDefaultVmoIndex, std::move(properties));
    PRESENT(flatland, false);
  }

  // The buffer collection ID must also be valid.
  {
    ImageProperties properties;
    flatland.CreateImage(1, 0, kDefaultVmoIndex, std::move(properties));
    PRESENT(flatland, false);
  }

  // The buffer collection can fail to validate.
  {
    EXPECT_CALL(*mock_renderer_, Validate(kGlobalBufferCollectionId))
        .WillOnce(Return(std::nullopt));

    ImageProperties properties;
    flatland.CreateImage(1, kBufferCollectionId, kDefaultVmoIndex, std::move(properties));
    PRESENT(flatland, false);
  }

  // The remaining error cases tested below occur after the BufferCollection is validated. Because
  // the results of validation is cached, we set up valid ranges for all properties once here.
  BufferCollectionMetadata metadata;
  metadata.vmo_count = 2;
  metadata.image_constraints.min_coded_width = 50;
  metadata.image_constraints.max_coded_width = 150;
  metadata.image_constraints.min_coded_height = 500;
  metadata.image_constraints.max_coded_height = 1500;
  EXPECT_CALL(*mock_renderer_, Validate(kGlobalBufferCollectionId)).WillOnce(Return(metadata));

  // The vmo index must be less than the vmo count.
  {
    ImageProperties properties;
    flatland.CreateImage(1, kBufferCollectionId, 3, std::move(properties));
    PRESENT(flatland, false);
  }

  // The width must be set.
  {
    ImageProperties properties;
    properties.set_height(kDefaultHeight);

    flatland.CreateImage(1, kBufferCollectionId, kDefaultVmoIndex, std::move(properties));
    PRESENT(flatland, false);
  }

  // The width must be within the valid range.
  {
    ImageProperties properties;
    properties.set_width(10);
    properties.set_height(kDefaultHeight);

    flatland.CreateImage(1, kBufferCollectionId, kDefaultVmoIndex, std::move(properties));
    PRESENT(flatland, false);
  }
  {
    ImageProperties properties;
    properties.set_width(1000);
    properties.set_height(kDefaultHeight);

    flatland.CreateImage(1, kBufferCollectionId, kDefaultVmoIndex, std::move(properties));
    PRESENT(flatland, false);
  }

  // The height must be set.
  {
    ImageProperties properties;
    properties.set_width(kDefaultWidth);

    flatland.CreateImage(1, kBufferCollectionId, kDefaultVmoIndex, std::move(properties));
    PRESENT(flatland, false);
  }

  // The height must be within the valid range.
  {
    ImageProperties properties;
    properties.set_width(kDefaultWidth);
    properties.set_height(100);

    flatland.CreateImage(1, kBufferCollectionId, kDefaultVmoIndex, std::move(properties));
    PRESENT(flatland, false);
  }
  {
    ImageProperties properties;
    properties.set_width(kDefaultWidth);
    properties.set_height(10000);

    flatland.CreateImage(1, kBufferCollectionId, kDefaultVmoIndex, std::move(properties));
    PRESENT(flatland, false);
  }

  // Two images cannot have the same ID.
  const ContentId kId = 1;
  {
    ImageProperties properties;
    properties.set_width(kDefaultWidth);
    properties.set_height(kDefaultHeight);

    flatland.CreateImage(kId, kBufferCollectionId, kDefaultVmoIndex, std::move(properties));
    PRESENT(flatland, true);
  }

  {
    ImageProperties properties;
    properties.set_width(kDefaultWidth);
    properties.set_height(kDefaultHeight);

    flatland.CreateImage(kId, kBufferCollectionId, kDefaultVmoIndex, std::move(properties));
    PRESENT(flatland, false);
  }

  // A Link id cannot be used for an image.
  const ContentId kLinkId = 2;
  {
    ContentLinkToken parent_token;
    GraphLinkToken child_token;
    ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

    fidl::InterfacePtr<ContentLink> content_link;
    LinkProperties link_properties;
    link_properties.set_logical_size({kDefaultSize, kDefaultSize});
    flatland.CreateLink(kLinkId, std::move(parent_token), std::move(link_properties),
                        content_link.NewRequest());
    PRESENT(flatland, true);

    ImageProperties image_properties;
    image_properties.set_width(kDefaultWidth);
    image_properties.set_height(kDefaultHeight);

    flatland.CreateImage(kLinkId, kBufferCollectionId, kDefaultVmoIndex,
                         std::move(image_properties));
    PRESENT(flatland, false);
  }
}

TEST_F(FlatlandTest, SetContentOnTransformErrorCases) {
  Flatland flatland = CreateFlatland();

  // Setup a valid image.
  const ContentId kImageId = 1;
  const BufferCollectionId kBufferCollectionId = 1;
  const uint32_t kWidth = 100;
  const uint32_t kHeight = 200;

  ImageProperties properties;
  properties.set_width(kWidth);
  properties.set_height(kHeight);

  CreateImage(&flatland, kImageId, kBufferCollectionId, std::move(properties));

  // Create a transform.
  const TransformId kTransformId = 1;

  flatland.CreateTransform(kTransformId);
  PRESENT(flatland, true);

  // Zero is not a valid transform.
  flatland.SetContentOnTransform(kImageId, 0);
  PRESENT(flatland, false);

  // The transform must exist.
  flatland.SetContentOnTransform(kImageId, 2);
  PRESENT(flatland, false);

  // The image must exist.
  flatland.SetContentOnTransform(2, kTransformId);
  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, ClearImageOnTransform) {
  Flatland flatland = CreateFlatland();

  // Setup a valid image.
  const ContentId kImageId = 1;
  const BufferCollectionId kBufferCollectionId = 1;

  ImageProperties properties;
  properties.set_width(100);
  properties.set_height(200);

  const GlobalBufferCollectionId global_collection_id =
      CreateImage(&flatland, kImageId, kBufferCollectionId, std::move(properties));

  // Create a transform, make it the root transform, and attach the image.
  const TransformId kTransformId = 1;

  flatland.CreateTransform(kTransformId);
  flatland.SetRootTransform(kTransformId);
  flatland.SetContentOnTransform(kImageId, kTransformId);
  PRESENT(flatland, true);

  // The image should be the only entry in the image vector.
  auto data = ProcessMainLoop(flatland.GetRoot());
  EXPECT_EQ(data.image_vector.size(), 1ul);

  const auto& image_data = data.image_vector[0];
  EXPECT_EQ(image_data.collection_id, global_collection_id);

  // An ContentId of 0 indicates to remove any image on the specified transform.
  flatland.SetContentOnTransform(0, kTransformId);
  PRESENT(flatland, true);

  data = ProcessMainLoop(flatland.GetRoot());
  EXPECT_TRUE(data.image_vector.empty());
}

TEST_F(FlatlandTest, ImagesAppearInTopologicalOrder) {
  Flatland flatland = CreateFlatland();

  // Setup two valid images.
  const ContentId kImageId1 = 1;
  const BufferCollectionId kBufferCollectionId1 = 1;

  ImageProperties properties1;
  properties1.set_width(100);
  properties1.set_height(200);

  const GlobalBufferCollectionId global_collection_id1 =
      CreateImage(&flatland, kImageId1, kBufferCollectionId1, std::move(properties1));

  const ContentId kImageId2 = 2;
  const BufferCollectionId kBufferCollectionId2 = 2;

  ImageProperties properties2;
  properties2.set_width(300);
  properties2.set_height(400);

  const GlobalBufferCollectionId global_collection_id2 =
      CreateImage(&flatland, kImageId2, kBufferCollectionId2, std::move(properties2));

  // Create a root transform with two children.
  const TransformId kTransformId1 = 3;
  const TransformId kTransformId2 = 4;
  const TransformId kTransformId3 = 5;

  flatland.CreateTransform(kTransformId1);
  flatland.CreateTransform(kTransformId2);
  flatland.CreateTransform(kTransformId3);

  flatland.AddChild(kTransformId1, kTransformId2);
  flatland.AddChild(kTransformId1, kTransformId3);

  flatland.SetRootTransform(kTransformId1);
  PRESENT(flatland, true);

  // Attach image 1 to the root and the second child. Attach image 2 to the first child.
  flatland.SetContentOnTransform(kImageId1, kTransformId1);
  flatland.SetContentOnTransform(kImageId2, kTransformId2);
  flatland.SetContentOnTransform(kImageId1, kTransformId3);
  PRESENT(flatland, true);

  // The images should appear pre-order toplogically sorted: 1, 2, 1 again. The same image is
  // allowed to appear multiple times.
  auto data = ProcessMainLoop(flatland.GetRoot());
  EXPECT_EQ(data.image_vector.size(), 3ul);
  EXPECT_EQ(data.image_vector[0].collection_id, global_collection_id1);
  EXPECT_EQ(data.image_vector[1].collection_id, global_collection_id2);
  EXPECT_EQ(data.image_vector[2].collection_id, global_collection_id1);

  // Clearing the image from the parent removes the first entry of the list since images are
  // visited before children.
  flatland.SetContentOnTransform(0, kTransformId1);
  PRESENT(flatland, true);

  // Meaning the new list of images should be: 2, 1.
  data = ProcessMainLoop(flatland.GetRoot());
  EXPECT_EQ(data.image_vector.size(), 2ul);
  EXPECT_EQ(data.image_vector[0].collection_id, global_collection_id2);
  EXPECT_EQ(data.image_vector[1].collection_id, global_collection_id1);
}

TEST_F(FlatlandTest, DeregisterBufferCollectionErrorCases) {
  Flatland flatland = CreateFlatland();

  // Zero is not a buffer collection ID.
  flatland.DeregisterBufferCollection(0);
  PRESENT(flatland, false);

  // The buffer collection ID must exist.
  flatland.DeregisterBufferCollection(1);
  PRESENT(flatland, false);

  // A buffer collection cannot be deregistered twice.
  const GlobalBufferCollectionId kGlobalBufferCollectionId = 2;
  EXPECT_CALL(*mock_renderer_, RegisterTextureCollection(_, _))
      .WillOnce(Return(kGlobalBufferCollectionId));

  const BufferCollectionId kBufferCollectionId = 3;
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
  flatland.RegisterBufferCollection(kBufferCollectionId, std::move(token));
  PRESENT(flatland, true);

  flatland.DeregisterBufferCollection(kBufferCollectionId);
  PRESENT(flatland, true);

  EXPECT_CALL(*mock_renderer_, DeregisterCollection(kGlobalBufferCollectionId)).Times(1);
  flatland.SignalBufferReleaseFence();
  RunLoopUntilIdle();

  flatland.DeregisterBufferCollection(kBufferCollectionId);
  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, DeregisterMultipleBufferCollectionsSameEvent) {
  Flatland flatland = CreateFlatland();

  // Register the first buffer collection.
  const GlobalBufferCollectionId kGlobalBufferCollectionId1 = 1;
  EXPECT_CALL(*mock_renderer_, RegisterTextureCollection(_, _))
      .WillOnce(Return(kGlobalBufferCollectionId1));

  const BufferCollectionId kBufferCollectionId1 = 2;
  {
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
    flatland.RegisterBufferCollection(kBufferCollectionId1, std::move(token));
  }

  // Register the second buffer collection.
  const GlobalBufferCollectionId kGlobalBufferCollectionId2 = 3;
  EXPECT_CALL(*mock_renderer_, RegisterTextureCollection(_, _))
      .WillOnce(Return(kGlobalBufferCollectionId2));

  const BufferCollectionId kBufferCollectionId2 = 4;
  {
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
    flatland.RegisterBufferCollection(kBufferCollectionId2, std::move(token));
  }

  PRESENT(flatland, true);

  // Deregister both buffer collections so they're both witing on the release fence. Do it one at a
  // time so the Waits are registered on different calls to Present().
  flatland.DeregisterBufferCollection(kBufferCollectionId1);
  PRESENT(flatland, true);

  flatland.DeregisterBufferCollection(kBufferCollectionId2);
  PRESENT(flatland, true);

  // Signal the release fence.
  EXPECT_CALL(*mock_renderer_, DeregisterCollection(kGlobalBufferCollectionId1)).Times(1);
  EXPECT_CALL(*mock_renderer_, DeregisterCollection(kGlobalBufferCollectionId2)).Times(1);
  flatland.SignalBufferReleaseFence();
  RunLoopUntilIdle();
}

TEST_F(FlatlandTest, DeregisteredBufferCollectionIdCanBeReused) {
  Flatland flatland = CreateFlatland();

  // Create a valid BufferCollectionId.
  const GlobalBufferCollectionId kGlobalBufferCollectionId1 = 1;
  EXPECT_CALL(*mock_renderer_, RegisterTextureCollection(_, _))
      .WillOnce(Return(kGlobalBufferCollectionId1));

  const BufferCollectionId kBufferCollectionId = 2;
  {
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
    flatland.RegisterBufferCollection(kBufferCollectionId, std::move(token));
    PRESENT(flatland, true);
  }

  // Deregister it, but don't signal the release fence yet.
  EXPECT_CALL(*mock_renderer_, DeregisterCollection(kGlobalBufferCollectionId1)).Times(0);
  flatland.DeregisterBufferCollection(kBufferCollectionId);
  PRESENT(flatland, true);

  // Register another buffer collection with that same ID.
  const GlobalBufferCollectionId kGlobalBufferCollectionId2 = 3;
  EXPECT_CALL(*mock_renderer_, RegisterTextureCollection(_, _))
      .WillOnce(Return(kGlobalBufferCollectionId2));

  {
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token;
    flatland.RegisterBufferCollection(kBufferCollectionId, std::move(token));
    PRESENT(flatland, true);
  }

  // Signal the release fences, which should result deregister the first one from the renderer.
  EXPECT_CALL(*mock_renderer_, DeregisterCollection(kGlobalBufferCollectionId1)).Times(1);
  EXPECT_CALL(*mock_renderer_, DeregisterCollection(kGlobalBufferCollectionId2)).Times(0);
  flatland.SignalBufferReleaseFence();
  RunLoopUntilIdle();

  // Deregister the second one, signal the release fences, and verify the second global ID was
  // deregistered.
  flatland.DeregisterBufferCollection(kBufferCollectionId);
  PRESENT(flatland, true);

  EXPECT_CALL(*mock_renderer_, DeregisterCollection(kGlobalBufferCollectionId2)).Times(1);
  flatland.SignalBufferReleaseFence();
  RunLoopUntilIdle();
}

// Tests that a buffer collection is not deregistered from the Renderer until it is not referenced
// by any active Image (including released Images still on Transforms) and the release fence is
// signaled.
TEST_F(FlatlandTest, DeregisterBufferCollectionWaitsForReleaseFence) {
  Flatland flatland = CreateFlatland();

  // Setup a valid buffer collection and Image.
  const ContentId kImageId = 1;
  const BufferCollectionId kBufferCollectionId = 2;

  ImageProperties properties;
  properties.set_width(100);
  properties.set_height(200);

  const GlobalBufferCollectionId global_collection_id =
      CreateImage(&flatland, kImageId, kBufferCollectionId, std::move(properties));

  // Attach the Image to a transform.
  const TransformId kTransformId = 3;
  flatland.CreateTransform(kTransformId);
  flatland.SetRootTransform(kTransformId);
  flatland.SetContentOnTransform(kImageId, kTransformId);
  PRESENT(flatland, true);

  // Deregister the buffer collection, but ensure that the deregistration call on the Renderer has
  // not happened.
  EXPECT_CALL(*mock_renderer_, DeregisterCollection(global_collection_id)).Times(0);
  flatland.DeregisterBufferCollection(kBufferCollectionId);
  PRESENT(flatland, true);

  // Release the Image that referenced the buffer collection. Because the Image is still attached
  // to a Transform, the deregestration call should still not happen.
  EXPECT_CALL(*mock_renderer_, DeregisterCollection(global_collection_id)).Times(0);
  flatland.ReleaseImage(kImageId);
  PRESENT(flatland, true);

  // Remove the Image from the transform. This triggers the creation of the release fence, but
  // still does not result in a deregestration call.
  EXPECT_CALL(*mock_renderer_, DeregisterCollection(global_collection_id)).Times(0);
  flatland.SetContentOnTransform(0, kTransformId);
  PRESENT(flatland, true);

  // Signal the release fences, which results in the deregistration call,
  EXPECT_CALL(*mock_renderer_, DeregisterCollection(global_collection_id)).Times(1);
  flatland.SignalBufferReleaseFence();
  RunLoopUntilIdle();
}

TEST_F(FlatlandTest, ReleaseImageErrorCases) {
  Flatland flatland = CreateFlatland();

  // Zero is not a valid image ID.
  flatland.ReleaseImage(0);
  PRESENT(flatland, false);

  // The image must exist.
  flatland.ReleaseImage(1);
  PRESENT(flatland, false);

  // ContentId is not an Image.
  ContentLinkToken parent_token;
  GraphLinkToken child_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &parent_token.value, &child_token.value));

  const ContentId kLinkId = 2;

  fidl::InterfacePtr<ContentLink> content_link;
  LinkProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  flatland.CreateLink(kLinkId, std::move(parent_token), std::move(properties),
                      content_link.NewRequest());

  flatland.ReleaseImage(kLinkId);
  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, ReleasedImageRemainsUntilCleared) {
  Flatland flatland = CreateFlatland();

  // Setup a valid image.
  const ContentId kImageId = 1;
  const BufferCollectionId kBufferCollectionId = 1;

  ImageProperties properties1;
  properties1.set_width(100);
  properties1.set_height(200);

  const GlobalBufferCollectionId global_collection_id1 =
      CreateImage(&flatland, kImageId, kBufferCollectionId, std::move(properties1));

  // Create a transform, make it the root transform, and attach the image.
  const TransformId kTransformId = 2;

  flatland.CreateTransform(kTransformId);
  flatland.SetRootTransform(kTransformId);
  flatland.SetContentOnTransform(kImageId, kTransformId);
  PRESENT(flatland, true);

  // The image should appear in the global image vector.
  auto data = ProcessMainLoop(flatland.GetRoot());
  EXPECT_EQ(data.image_vector.size(), 1ul);
  EXPECT_EQ(data.image_vector[0].collection_id, global_collection_id1);

  // Releasing the image succeeds, but the image remains in the global image vector.
  flatland.ReleaseImage(kImageId);
  PRESENT(flatland, true);

  data = ProcessMainLoop(flatland.GetRoot());
  EXPECT_EQ(data.image_vector.size(), 1ul);
  EXPECT_EQ(data.image_vector[0].collection_id, global_collection_id1);

  // Clearing the Transform of its Image removes it from the global image vector.
  flatland.SetContentOnTransform(0, kTransformId);
  PRESENT(flatland, true);

  data = ProcessMainLoop(flatland.GetRoot());
  EXPECT_EQ(data.image_vector.size(), 0ul);
}

TEST_F(FlatlandTest, ReleasedImageIdCanBeReused) {
  Flatland flatland = CreateFlatland();

  // Setup a valid image.
  const ContentId kImageId = 1;
  const BufferCollectionId kBufferCollectionId = 1;

  ImageProperties properties1;
  properties1.set_width(100);
  properties1.set_height(200);

  const GlobalBufferCollectionId global_collection_id1 =
      CreateImage(&flatland, kImageId, kBufferCollectionId, std::move(properties1));

  // Create a transform, make it the root transform, attach the image, then release it.
  const TransformId kTransformId1 = 2;

  flatland.CreateTransform(kTransformId1);
  flatland.SetRootTransform(kTransformId1);
  flatland.SetContentOnTransform(kImageId, kTransformId1);
  flatland.ReleaseImage(kImageId);
  PRESENT(flatland, true);

  // The ContentId can be re-used even though the old image is still present. Add a second
  // transform so that both images show up in the global image vector.
  ImageProperties properties2;
  properties2.set_width(300);
  properties2.set_height(400);

  const GlobalBufferCollectionId global_collection_id2 =
      CreateImage(&flatland, kImageId, 2, std::move(properties2));

  const TransformId kTransformId2 = 3;

  flatland.CreateTransform(kTransformId2);
  flatland.AddChild(kTransformId1, kTransformId2);
  flatland.SetContentOnTransform(kImageId, kTransformId2);
  PRESENT(flatland, true);

  // The images appear in topological order.
  auto data = ProcessMainLoop(flatland.GetRoot());
  EXPECT_EQ(data.image_vector.size(), 2ul);
  EXPECT_EQ(data.image_vector[0].collection_id, global_collection_id1);
  EXPECT_EQ(data.image_vector[1].collection_id, global_collection_id2);
}

// Test that released Images, when attached to a Transform, are not garbage collected even if
// the Transform is not part of the most recently presented global topology.
TEST_F(FlatlandTest, ReleasedImagePersistsOutsideGlobalTopology) {
  Flatland flatland = CreateFlatland();

  // Setup a valid image.
  const ContentId kImageId = 1;
  const BufferCollectionId kBufferCollectionId = 1;

  ImageProperties properties1;
  properties1.set_width(100);
  properties1.set_height(200);

  const GlobalBufferCollectionId global_collection_id1 =
      CreateImage(&flatland, kImageId, kBufferCollectionId, std::move(properties1));

  // Create a transform, make it the root transform, attach the image, then release it.
  const TransformId kTransformId = 2;

  flatland.CreateTransform(kTransformId);
  flatland.SetRootTransform(kTransformId);
  flatland.SetContentOnTransform(kImageId, kTransformId);
  flatland.ReleaseImage(kImageId);
  PRESENT(flatland, true);

  // Remove the entire hierarchy, then verify that no images are present.
  flatland.SetRootTransform(0);
  PRESENT(flatland, true);

  auto data = ProcessMainLoop(flatland.GetRoot());
  EXPECT_EQ(data.image_vector.size(), 0ul);

  // Reintroduce the hierarchy and confirm the Image is still present, even though it was
  // temporarily not reachable from the root transform.
  flatland.SetRootTransform(kTransformId);
  PRESENT(flatland, true);

  data = ProcessMainLoop(flatland.GetRoot());
  EXPECT_EQ(data.image_vector.size(), 1ul);
  EXPECT_EQ(data.image_vector[0].collection_id, global_collection_id1);
}

TEST_F(FlatlandTest, ClearGraphReleasesImagesAndBufferCollections) {
  Flatland flatland = CreateFlatland();

  // Setup a valid image.
  const ContentId kImageId = 1;
  const BufferCollectionId kBufferCollectionId = 1;

  ImageProperties properties1;
  properties1.set_width(100);
  properties1.set_height(200);

  const GlobalBufferCollectionId global_collection_id1 =
      CreateImage(&flatland, kImageId, kBufferCollectionId, std::move(properties1));

  // Create a transform, make it the root transform, and attach the Image.
  const TransformId kTransformId = 2;

  flatland.CreateTransform(kTransformId);
  flatland.SetRootTransform(kTransformId);
  flatland.SetContentOnTransform(kImageId, kTransformId);
  PRESENT(flatland, true);

  // Clear the graph, then signal the release fence and ensure the buffer collection is released.
  flatland.ClearGraph();
  PRESENT(flatland, true);

  EXPECT_CALL(*mock_renderer_, DeregisterCollection(global_collection_id1)).Times(1);
  flatland.SignalBufferReleaseFence();
  RunLoopUntilIdle();

  // The buffer collection and Image ID should be available for re-use.
  ImageProperties properties2;
  properties2.set_width(400);
  properties2.set_height(800);

  const GlobalBufferCollectionId global_collection_id2 =
      CreateImage(&flatland, kImageId, kBufferCollectionId, std::move(properties2));

  EXPECT_NE(global_collection_id1, global_collection_id2);

  // Verify that the Image is valid and can be attached to a transform.
  flatland.CreateTransform(kTransformId);
  flatland.SetRootTransform(kTransformId);
  flatland.SetContentOnTransform(kImageId, kTransformId);
  PRESENT(flatland, true);
}

#undef EXPECT_MATRIX
#undef PRESENT

}  // namespace test
}  // namespace flatland
