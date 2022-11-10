// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/flatland.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/time.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_creation_tokens.h>
#include <lib/ui/scenic/cpp/view_identity.h>

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "fuchsia/ui/composition/cpp/fidl.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/scenic/lib/allocation/allocator.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/allocation/mock_buffer_collection_importer.h"
#include "src/ui/scenic/lib/flatland/flatland_display.h"
#include "src/ui/scenic/lib/flatland/global_matrix_data.h"
#include "src/ui/scenic/lib/flatland/global_topology_data.h"
#include "src/ui/scenic/lib/flatland/tests/mock_flatland_presenter.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"
#include "src/ui/scenic/lib/utils/dispatcher_holder.h"
#include "src/ui/scenic/lib/utils/helpers.h"

#include <glm/gtx/matrix_transform_2d.hpp>

using ::testing::_;
using ::testing::Return;

using BufferCollectionId = flatland::Flatland::BufferCollectionId;
using allocation::Allocator;
using allocation::BufferCollectionImporter;
using allocation::BufferCollectionImportExportTokens;
using allocation::ImageMetadata;
using allocation::MockBufferCollectionImporter;
using flatland::Flatland;
using flatland::FlatlandDisplay;
using flatland::FlatlandPresenter;
using flatland::GlobalMatrixVector;
using flatland::GlobalTopologyData;
using flatland::LinkSystem;
using flatland::MockFlatlandPresenter;
using flatland::TransformGraph;
using flatland::TransformHandle;
using flatland::UberStruct;
using flatland::UberStructSystem;
using fuchsia::math::SizeU;
using fuchsia::ui::composition::Allocator_RegisterBufferCollection_Result;
using fuchsia::ui::composition::BufferCollectionExportToken;
using fuchsia::ui::composition::BufferCollectionImportToken;
using fuchsia::ui::composition::ChildViewStatus;
using fuchsia::ui::composition::ChildViewWatcher;
using fuchsia::ui::composition::ContentId;
using fuchsia::ui::composition::FlatlandError;
using fuchsia::ui::composition::ImageProperties;
using fuchsia::ui::composition::LayoutInfo;
using fuchsia::ui::composition::OnNextFrameBeginValues;
using fuchsia::ui::composition::Orientation;
using fuchsia::ui::composition::ParentViewportStatus;
using fuchsia::ui::composition::ParentViewportWatcher;
using fuchsia::ui::composition::TransformId;
using fuchsia::ui::composition::ViewportProperties;
using fuchsia::ui::views::ViewCreationToken;
using fuchsia::ui::views::ViewportCreationToken;

namespace {

// Convenience struct for the PRESENT_WITH_ARGS macro to avoid having to update it every time
// a new argument is added to Flatland::Present(). This struct also includes additional flags
// to PRESENT_WITH_ARGS itself for testing timing-related Present() functionality.
struct PresentArgs {
  // Arguments to Flatland::Present().
  zx::time requested_presentation_time;
  std::vector<zx::event> acquire_fences;
  std::vector<zx::event> release_fences;
  bool unsquashable = false;

  // Arguments to the PRESENT_WITH_ARGS macro.

  // If true, skips the session update associated with the Present(), meaning the new UberStruct
  // will not be in the snapshot and the release fences will not be signaled.
  bool skip_session_update_and_release_fences = false;

  // The number of present tokens that should be returned to the client.
  uint32_t present_credits_returned = 1;

  // The future presentation infos that should be returned to the client.
  flatland::Flatland::FuturePresentationInfos presentation_infos = {};

  // If PRESENT_WITH_ARGS is called with |expect_success| = false, the error that should be
  // expected as the return value from Present().
  FlatlandError expected_error = FlatlandError::BAD_OPERATION;
};

struct GlobalIdPair {
  allocation::GlobalBufferCollectionId collection_id;
  allocation::GlobalImageId image_id;
};

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
#define PRESENT_WITH_ARGS(flatland, args, expect_success)                                   \
  {                                                                                         \
    bool had_acquire_fences = !(args).acquire_fences.empty();                               \
    bool processed_callback = false;                                                        \
    fuchsia::ui::composition::PresentArgs present_args;                                     \
    present_args.set_requested_presentation_time((args).requested_presentation_time.get()); \
    present_args.set_acquire_fences(std::move((args).acquire_fences));                      \
    present_args.set_release_fences(std::move((args).release_fences));                      \
    present_args.set_unsquashable((args).unsquashable);                                     \
    (flatland)->Present(std::move(present_args));                                           \
    if (expect_success) {                                                                   \
      /* Even with no acquire_fences, UberStruct updates queue on the dispatcher. */        \
      if (!had_acquire_fences) {                                                            \
        EXPECT_CALL(*mock_flatland_presenter_,                                              \
                    ScheduleUpdateForSession((args).requested_presentation_time, _,         \
                                             (args).unsquashable, _));                      \
      }                                                                                     \
      RunLoopUntilIdle();                                                                   \
      if (!(args).skip_session_update_and_release_fences) {                                 \
        ApplySessionUpdatesAndSignalFences();                                               \
      }                                                                                     \
      (flatland)->OnNextFrameBegin((args).present_credits_returned,                         \
                                   std::move((args).presentation_infos));                   \
    } else {                                                                                \
      RunLoopUntilIdle();                                                                   \
      EXPECT_EQ(GetPresentError((flatland)->GetSessionId()), (args).expected_error);        \
    }                                                                                       \
  }

// Identical to PRESENT_WITH_ARGS, but supplies an empty PresentArgs to the Present() call.
#define PRESENT(flatland, expect_success) \
  { PRESENT_WITH_ARGS(flatland, PresentArgs(), expect_success); }

#define REGISTER_BUFFER_COLLECTION(allocator, export_token, token, expect_success)               \
  if (expect_success) {                                                                          \
    EXPECT_CALL(*mock_buffer_collection_importer_,                                               \
                ImportBufferCollection(fsl::GetKoid(export_token.value.get()), _, _, _, _))      \
        .WillOnce(testing::Invoke(                                                               \
            [](allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,           \
               fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>,                    \
               allocation::BufferCollectionUsage,                                                \
               std::optional<fuchsia::math::SizeU>) { return true; }));                          \
  }                                                                                              \
  bool processed_callback = false;                                                               \
  fuchsia::ui::composition::RegisterBufferCollectionArgs args;                                   \
  args.set_export_token(std::move(export_token));                                                \
  args.set_buffer_collection_token(token);                                                       \
  allocator->RegisterBufferCollection(                                                           \
      std::move(args), [&processed_callback](Allocator_RegisterBufferCollection_Result result) { \
        EXPECT_EQ(!expect_success, result.is_err());                                             \
        processed_callback = true;                                                               \
      });                                                                                        \
  EXPECT_TRUE(processed_callback);

// This macro searches for a local matrix associated with a specific TransformHandle.
//
// |uber_struct| is the UberStruct to search to find the matrix. |target_handle| is the
// TransformHandle of the matrix to compare. |expected_matrix| is the expected value of that
// matrix.
#define EXPECT_MATRIX(uber_struct, target_handle, expected_matrix)                               \
  {                                                                                              \
    glm::mat3 matrix = glm::mat3();                                                              \
    auto matrix_kv = uber_struct->local_matrices.find(target_handle);                            \
    if (matrix_kv != uber_struct->local_matrices.end()) {                                        \
      matrix = matrix_kv->second;                                                                \
    }                                                                                            \
    for (size_t i = 0; i < 3; ++i) {                                                             \
      for (size_t j = 0; j < 3; ++j) {                                                           \
        EXPECT_FLOAT_EQ(matrix[i][j], expected_matrix[i][j]) << " row " << j << " column " << i; \
      }                                                                                          \
    }                                                                                            \
  }

const uint32_t kDefaultSize = 1;
const glm::vec2 kDefaultDisplayPixelRatio = {1.0f, 1.0f};
const int32_t kDefaultInset = 0;

void ExpectRectFEquals(const fuchsia::math::RectF& rect1, const fuchsia::math::RectF& rect2) {
  EXPECT_FLOAT_EQ(rect1.x, rect2.x);
  EXPECT_FLOAT_EQ(rect1.y, rect2.y);
  EXPECT_FLOAT_EQ(rect1.width, rect2.width);
  EXPECT_FLOAT_EQ(rect1.height, rect2.height);
}

fuchsia::ui::composition::ViewBoundProtocols NoViewProtocols() { return {}; }

// Testing FlatlandDisplay requires much of the same setup as testing Flatland, so we use the same
// test fixture class (defined immediately below), but renamed to group FlatlandDisplay tests.
class FlatlandTest;
using FlatlandDisplayTest = FlatlandTest;

class FlatlandTest : public gtest::TestLoopFixture {
 public:
  FlatlandTest()
      : uber_struct_system_(std::make_shared<UberStructSystem>()),
        link_system_(std::make_shared<LinkSystem>(uber_struct_system_->GetNextInstanceId())) {}

  void SetUp() override {
    mock_flatland_presenter_ = new ::testing::StrictMock<MockFlatlandPresenter>();

    ON_CALL(*mock_flatland_presenter_, ScheduleUpdateForSession(_, _, _, _))
        .WillByDefault(::testing::Invoke(
            [&](zx::time requested_presentation_time, scheduling::SchedulingIdPair id_pair,
                bool unsquashable, std::vector<zx::event> release_fences) {
              // The ID must not already be registered.
              EXPECT_FALSE(pending_release_fences_.find(id_pair) != pending_release_fences_.end());
              pending_release_fences_[id_pair] = std::move(release_fences);

              // Ensure IDs are strictly increasing.
              auto current_id_kv = pending_session_updates_.find(id_pair.session_id);
              EXPECT_TRUE(current_id_kv == pending_session_updates_.end() ||
                          current_id_kv->second < id_pair.present_id);

              // Only save the latest PresentId: the UberStructSystem will flush all Presents prior
              // to it.
              pending_session_updates_[id_pair.session_id] = id_pair.present_id;

              // Store all requested presentation times to verify in test.
              requested_presentation_times_[id_pair] = requested_presentation_time;
            }));

    sysmem_allocator_ = utils::CreateSysmemAllocatorSyncPtr("FlatlandTest::SetUp");

    flatland_presenter_ = std::shared_ptr<FlatlandPresenter>(mock_flatland_presenter_);

    mock_buffer_collection_importer_ = new MockBufferCollectionImporter();
    buffer_collection_importer_ =
        std::shared_ptr<allocation::BufferCollectionImporter>(mock_buffer_collection_importer_);

    // Capture uninteresting cleanup calls from Allocator dtor.
    EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(_, _))
        .Times(::testing::AtLeast(0));
  }

  void TearDown() override {
    RunLoopUntilIdle();

    auto link_topologies = link_system_->GetResolvedTopologyLinks();
    EXPECT_TRUE(link_topologies.empty());

    buffer_collection_importer_.reset();
    flatland_presenter_.reset();
    flatlands_.clear();
    flatland_displays_.clear();
  }

  std::shared_ptr<Allocator> CreateAllocator() {
    std::vector<std::shared_ptr<BufferCollectionImporter>> importers;
    std::vector<std::shared_ptr<BufferCollectionImporter>> screenshot_importers;
    importers.push_back(buffer_collection_importer_);
    return std::make_shared<Allocator>(
        context_provider_.context(), importers, screenshot_importers,
        utils::CreateSysmemAllocatorSyncPtr("FlatlandTest::CreateAllocator"));
  }

  std::shared_ptr<Flatland> CreateFlatland() {
    auto session_id = scheduling::GetNextSessionId();
    flatlands_.push_back({});
    std::vector<std::shared_ptr<BufferCollectionImporter>> importers;
    importers.push_back(buffer_collection_importer_);
    std::shared_ptr<Flatland> flatland = Flatland::New(
        std::make_shared<utils::UnownedDispatcherHolder>(dispatcher()),
        flatlands_.back().NewRequest(), session_id,
        /*destroy_instance_functon=*/[this, session_id]() { flatland_errors_.erase(session_id); },
        flatland_presenter_, link_system_, uber_struct_system_->AllocateQueueForSession(session_id),
        importers, [](auto...) {}, [](auto...) {}, [](auto...) {}, [](auto...) {});

    // Register OnNextFrameBegin() callback to capture errors.
    RegisterPresentError(flatlands_.back(), session_id);
    return flatland;
  }

  std::shared_ptr<FlatlandDisplay> CreateFlatlandDisplay(uint32_t width_in_px,
                                                         uint32_t height_in_px) {
    auto session_id = scheduling::GetNextSessionId();
    auto display =
        std::make_shared<scenic_impl::display::Display>(/*id*/ 1, width_in_px, height_in_px);
    flatland_displays_.push_back({});
    return FlatlandDisplay::New(
        std::make_shared<utils::UnownedDispatcherHolder>(dispatcher()),
        flatland_displays_.back().NewRequest(), session_id, std::move(display),
        /*destroy_display_function*/ []() {}, flatland_presenter_, link_system_,
        uber_struct_system_->AllocateQueueForSession(session_id));
  }

  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> CreateToken() {
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token;
    zx_status_t status = sysmem_allocator_->AllocateSharedCollection(token.NewRequest());
    EXPECT_EQ(status, ZX_OK);
    status = token->Sync();
    EXPECT_EQ(status, ZX_OK);
    return token;
  }

  // Applies the most recently scheduled session update for each session and signals the release
  // fences of all Presents up to and including that update.
  void ApplySessionUpdatesAndSignalFences() {
    uber_struct_system_->UpdateSessions(pending_session_updates_);

    // Signal all release fences up to and including the PresentId in |pending_session_updates_|.
    for (const auto& [session_id, present_id] : pending_session_updates_) {
      auto begin = pending_release_fences_.lower_bound({session_id, 0});
      auto end = pending_release_fences_.upper_bound({session_id, present_id});
      for (auto fences_kv = begin; fences_kv != end; ++fences_kv) {
        for (auto& event : fences_kv->second) {
          event.signal(0, ZX_EVENT_SIGNALED);
        }
      }
      pending_release_fences_.erase(begin, end);
    }

    pending_session_updates_.clear();
    requested_presentation_times_.clear();
  }

  // Gets the list of registered PresentIds for a particular |session_id|.
  std::vector<scheduling::PresentId> GetRegisteredPresents(scheduling::SessionId session_id) const {
    std::vector<scheduling::PresentId> present_ids;

    auto begin = pending_release_fences_.lower_bound({session_id, 0});
    auto end = pending_release_fences_.upper_bound({session_id + 1, 0});
    for (auto fence_kv = begin; fence_kv != end; ++fence_kv) {
      present_ids.push_back(fence_kv->first.present_id);
    }

    return present_ids;
  }

  // Returns true if |session_id| currently has a session update pending.
  bool HasSessionUpdate(scheduling::SessionId session_id) const {
    return pending_session_updates_.count(session_id);
  }

  // Returns the requested presentation time for a particular |id_pair|, or std::nullopt if that
  // pair has not had a presentation scheduled for it.
  std::optional<zx::time> GetRequestedPresentationTime(scheduling::SchedulingIdPair id_pair) {
    auto iter = requested_presentation_times_.find(id_pair);
    if (iter == requested_presentation_times_.end()) {
      return std::nullopt;
    }
    return iter->second;
  }

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

  // Snapshots the UberStructSystem and fetches the UberStruct associated with |flatland|. If no
  // UberStruct exists for |flatland|, returns nullptr.
  std::shared_ptr<UberStruct> GetUberStruct(Flatland* flatland) {
    auto snapshot = uber_struct_system_->Snapshot();

    auto root = flatland->GetRoot();
    auto uber_struct_kv = snapshot.find(root.GetInstanceId());
    if (uber_struct_kv == snapshot.end()) {
      return nullptr;
    }

    auto uber_struct = uber_struct_kv->second;
    EXPECT_FALSE(uber_struct->local_topology.empty());
    EXPECT_EQ(uber_struct->local_topology[0].handle, root);

    return uber_struct;
  }

  // Updates all Links reachable from |root_transform|, which must be the root transform of one of
  // the active Flatland instances.
  //
  // Tests that call this function are testing both Flatland and LinkSystem::UpdateLinks().
  void UpdateLinks(TransformHandle root_transform) {
    // Run the looper in case there are queued commands in, e.g., ObjectLinker.
    RunLoopUntilIdle();

    // This is a replica of the core render loop.
    const auto snapshot = uber_struct_system_->Snapshot();
    const auto links = link_system_->GetResolvedTopologyLinks();
    const auto data = GlobalTopologyData::ComputeGlobalTopologyData(
        snapshot, links, link_system_->GetInstanceId(), root_transform);
    const auto matrices =
        flatland::ComputeGlobalMatrices(data.topology_vector, data.parent_indices, snapshot);

    link_system_->UpdateLinks(data.topology_vector, data.live_handles, matrices,
                              display_pixel_ratio_, snapshot);

    // Run the looper again to process any queued FIDL events (i.e., Link callbacks).
    RunLoopUntilIdle();
  }

  void CreateViewport(Flatland* parent, Flatland* child, ContentId id,
                      fidl::InterfacePtr<ChildViewWatcher>* child_view_watcher,
                      fidl::InterfacePtr<ParentViewportWatcher>* parent_viewport_watcher) {
    ViewportCreationToken parent_token;
    ViewCreationToken child_token;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

    ViewportProperties properties;
    properties.set_logical_size({kDefaultSize, kDefaultSize});
    parent->CreateViewport(id, std::move(parent_token), std::move(properties),
                           child_view_watcher->NewRequest());
    child->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(),
                       NoViewProtocols(), parent_viewport_watcher->NewRequest());
    PRESENT(parent, true);
    PRESENT(child, true);

    // After View creation the child should have an associated ViewRef.
    auto child_uber_struct = GetUberStruct(child);
    ASSERT_NE(child_uber_struct, nullptr);
    EXPECT_NE(child_uber_struct->view_ref, nullptr);
  }

  void SetDisplayContent(FlatlandDisplay* display, Flatland* child,
                         fidl::InterfacePtr<ChildViewWatcher>* child_view_watcher,
                         fidl::InterfacePtr<ParentViewportWatcher>* parent_viewport_watcher) {
    FX_CHECK(display);
    FX_CHECK(child);
    FX_CHECK(child_view_watcher);
    FX_CHECK(parent_viewport_watcher);
    ViewportCreationToken parent_token;
    ViewCreationToken child_token;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));
    auto present_id = scheduling::PeekNextPresentId();
    EXPECT_CALL(
        *mock_flatland_presenter_,
        ScheduleUpdateForSession(
            zx::time(0), scheduling::SchedulingIdPair{display->session_id(), present_id}, true, _));
    display->SetContent(std::move(parent_token), child_view_watcher->NewRequest());
    child->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(),
                       NoViewProtocols(), parent_viewport_watcher->NewRequest());
  }

  // Creates an image in |flatland| with the specified |image_id| and backing properties.
  // This function also returns the GlobalBufferCollectionId that will be in the ImageMetadata
  // struct for that Image.
  GlobalIdPair CreateImage(
      Flatland* flatland, Allocator* allocator, ContentId image_id,
      BufferCollectionImportExportTokens buffer_collection_import_export_tokens,
      ImageProperties properties) {
    const auto koid = fsl::GetKoid(buffer_collection_import_export_tokens.export_token.value.get());
    REGISTER_BUFFER_COLLECTION(allocator, buffer_collection_import_export_tokens.export_token,
                               CreateToken(), true);

    FX_DCHECK(properties.has_size());
    FX_DCHECK(properties.size().width);
    FX_DCHECK(properties.size().height);

    allocation::GlobalImageId global_image_id;
    EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _))
        .WillOnce(testing::Invoke([&global_image_id](const ImageMetadata& metadata,
                                                     allocation::BufferCollectionUsage usage_type) {
          global_image_id = metadata.identifier;
          return true;
        }));

    flatland->CreateImage(image_id, std::move(buffer_collection_import_export_tokens.import_token),
                          0, std::move(properties));
    PRESENT(flatland, true);
    return {.collection_id = koid, .image_id = global_image_id};
  }

  // Returns FlatlandError code passed returned to OnNextFrameBegin() for |flatland|.
  FlatlandError GetPresentError(scheduling::SessionId session_id) {
    return flatland_errors_[session_id];
  }

  void RegisterPresentError(fuchsia::ui::composition::FlatlandPtr& flatland_channel,
                            scheduling::SessionId session_id) {
    flatland_channel.events().OnError = [this, session_id](FlatlandError error) {
      flatland_errors_[session_id] = error;
    };
  }

 protected:
  ::testing::StrictMock<MockFlatlandPresenter>* mock_flatland_presenter_;
  MockBufferCollectionImporter* mock_buffer_collection_importer_;
  std::shared_ptr<allocation::BufferCollectionImporter> buffer_collection_importer_;
  const std::shared_ptr<UberStructSystem> uber_struct_system_;
  std::shared_ptr<FlatlandPresenter> flatland_presenter_;
  const std::shared_ptr<LinkSystem> link_system_;
  sys::testing::ComponentContextProvider context_provider_;

 private:
  std::vector<fuchsia::ui::composition::FlatlandPtr> flatlands_;
  std::vector<fuchsia::ui::composition::FlatlandDisplayPtr> flatland_displays_;
  std::unordered_map<scheduling::SessionId, FlatlandError> flatland_errors_;
  glm::vec2 display_pixel_ratio_ = kDefaultDisplayPixelRatio;

  // Storage for |mock_flatland_presenter_|.
  std::map<scheduling::SchedulingIdPair, std::vector<zx::event>> pending_release_fences_;
  std::map<scheduling::SchedulingIdPair, zx::time> requested_presentation_times_;
  std::unordered_map<scheduling::SessionId, scheduling::PresentId> pending_session_updates_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

}  // namespace

namespace flatland {
namespace test {

TEST_F(FlatlandTest, PresentShouldReturnSuccess) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();
  PRESENT(flatland, true);
}

TEST_F(FlatlandTest, PresentErrorNoTokens) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Present, but return no tokens so the client has none left.
  {
    PresentArgs args;
    args.present_credits_returned = 0;
    PRESENT_WITH_ARGS(flatland, std::move(args), true);
  }

  // Present again, which should fail because the client has no tokens.
  {
    PresentArgs args;
    args.expected_error = FlatlandError::NO_PRESENTS_REMAINING;
    PRESENT_WITH_ARGS(flatland, std::move(args), false);
  }
}

TEST_F(FlatlandTest, MultiplePresentTokensAvailable) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Return one extra present token, meaning the instance now has two.
  flatland->OnNextFrameBegin(1, {});

  // Present, but return no tokens so the client has only one left.
  {
    PresentArgs args;
    args.present_credits_returned = 0;
    PRESENT_WITH_ARGS(flatland, std::move(args), true);
  }

  // Present again, which should succeed because the client already has an extra token even though
  // the previous PRESENT_WITH_ARGS returned none.
  {
    PresentArgs args;
    args.present_credits_returned = 0;
    PRESENT_WITH_ARGS(flatland, std::move(args), true);
  }

  // A third Present() will fail since the previous two calls consumed the two tokens.
  {
    PresentArgs args;
    args.expected_error = FlatlandError::NO_PRESENTS_REMAINING;
    PRESENT_WITH_ARGS(flatland, std::move(args), false);
  }
}

TEST_F(FlatlandTest, PresentWithNoFieldsSet) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  const bool kDefaultUnsquashable = false;
  const zx::time kDefaultRequestedPresentationTime = zx::time(0);

  fuchsia::ui::composition::PresentArgs present_args;
  flatland->Present(std::move(present_args));

  EXPECT_CALL(*mock_flatland_presenter_, ScheduleUpdateForSession(kDefaultRequestedPresentationTime,
                                                                  _, kDefaultUnsquashable, _));
  RunLoopUntilIdle();
}

TEST_F(FlatlandTest, PresentWaitsForAcquireFences) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Create two events to serve as acquire fences.
  PresentArgs args;
  args.acquire_fences = utils::CreateEventArray(2);
  auto acquire1_copy = utils::CopyEvent(args.acquire_fences[0]);
  auto acquire2_copy = utils::CopyEvent(args.acquire_fences[1]);

  // Create an event to serve as a release fence.
  args.release_fences = utils::CreateEventArray(1);
  auto release_copy = utils::CopyEvent(args.release_fences[0]);

  // Because the Present includes unsignaled acquire fences, the UberStructSystem shouldn't have any
  // entries and applying session updates shouldn't signal the release fence.
  PRESENT_WITH_ARGS(flatland, std::move(args), true);

  auto registered_presents = GetRegisteredPresents(flatland->GetRoot().GetInstanceId());
  EXPECT_EQ(registered_presents.size(), 0ul);
  EXPECT_EQ(GetUberStruct(flatland.get()), nullptr);
  EXPECT_FALSE(utils::IsEventSignalled(release_copy, ZX_EVENT_SIGNALED));

  // Signal the second fence and verify the UberStructSystem doesn't update, and the release fence
  // isn't signaled.
  acquire2_copy.signal(0, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  ApplySessionUpdatesAndSignalFences();

  registered_presents = GetRegisteredPresents(flatland->GetRoot().GetInstanceId());
  EXPECT_EQ(registered_presents.size(), 0ul);
  EXPECT_EQ(GetUberStruct(flatland.get()), nullptr);
  EXPECT_FALSE(utils::IsEventSignalled(release_copy, ZX_EVENT_SIGNALED));

  // Signal the first fence and verify that, the UberStructSystem contains an UberStruct for the
  // instance, and the release fence is signaled.
  acquire1_copy.signal(0, ZX_EVENT_SIGNALED);

  EXPECT_CALL(*mock_flatland_presenter_, ScheduleUpdateForSession(_, _, _, _));
  RunLoopUntilIdle();

  // After signaling the final acquire fence (and running the loop), there is now a registered
  // present.  There is still no UberStruct, nor has the release fence been signalled, because
  // session updates haven't been applied.
  registered_presents = GetRegisteredPresents(flatland->GetRoot().GetInstanceId());
  EXPECT_EQ(registered_presents.size(), 1ul);
  EXPECT_EQ(GetUberStruct(flatland.get()), nullptr);
  EXPECT_FALSE(utils::IsEventSignalled(release_copy, ZX_EVENT_SIGNALED));

  ApplySessionUpdatesAndSignalFences();

  // There is no longer a registered present; it was removed because it was processed when
  // session updates were applied.  There is now an UberStruct, and the release fence has been
  // signaled.
  registered_presents = GetRegisteredPresents(flatland->GetRoot().GetInstanceId());
  EXPECT_EQ(registered_presents.size(), 0ul);
  EXPECT_NE(GetUberStruct(flatland.get()), nullptr);
  EXPECT_TRUE(utils::IsEventSignalled(release_copy, ZX_EVENT_SIGNALED));
}

TEST_F(FlatlandTest, PresentForwardsRequestedPresentationTime) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Create an event to serve as an acquire fence.
  const zx::time requested_presentation_time = zx::time(123);

  PresentArgs args;
  args.requested_presentation_time = requested_presentation_time;
  args.acquire_fences = utils::CreateEventArray(1);
  auto acquire_copy = utils::CopyEvent(args.acquire_fences[0]);

  // Because the Present includes acquire fences, it isn't registered with the presenter immediately
  // upon `Present()`.
  auto present_id = scheduling::PeekNextPresentId();
  PRESENT_WITH_ARGS(flatland, std::move(args), true);

  auto registered_presents = GetRegisteredPresents(flatland->GetRoot().GetInstanceId());
  EXPECT_EQ(registered_presents.size(), 0ul);

  const auto id_pair = scheduling::SchedulingIdPair({
      .session_id = flatland->GetRoot().GetInstanceId(),
      .present_id = present_id,
  });

  auto maybe_presentation_time = GetRequestedPresentationTime(id_pair);
  EXPECT_FALSE(maybe_presentation_time.has_value());

  // Signal the fence and ensure the Present is still registered, but now with a requested
  // presentation time.
  acquire_copy.signal(0, ZX_EVENT_SIGNALED);

  EXPECT_CALL(*mock_flatland_presenter_, ScheduleUpdateForSession(_, _, _, _));
  RunLoopUntilIdle();

  registered_presents = GetRegisteredPresents(flatland->GetRoot().GetInstanceId());
  EXPECT_EQ(registered_presents.size(), 1ul);

  maybe_presentation_time = GetRequestedPresentationTime(id_pair);
  EXPECT_TRUE(maybe_presentation_time.has_value());
  EXPECT_EQ(maybe_presentation_time.value(), requested_presentation_time);
}

TEST_F(FlatlandTest, PresentWithSignaledFencesUpdatesImmediately) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Create an event to serve as the acquire fence.
  PresentArgs args;
  args.acquire_fences = utils::CreateEventArray(1);
  auto acquire_copy = utils::CopyEvent(args.acquire_fences[0]);

  // Create an event to serve as a release fence.
  args.release_fences = utils::CreateEventArray(1);
  auto release_copy = utils::CopyEvent(args.release_fences[0]);

  // Signal the event before the Present() call.
  acquire_copy.signal(0, ZX_EVENT_SIGNALED);

  // The PresentId is no longer registered because it has been applied, the UberStructSystem should
  // update immediately, and the release fence should be signaled. The PRESENT macro only expects
  // the ScheduleUpdateForSession() call when no acquire fences are present, but since this test
  // specifically tests pre-signaled fences, the EXPECT_CALL must be added here.
  EXPECT_CALL(*mock_flatland_presenter_, ScheduleUpdateForSession(_, _, _, _));
  PRESENT_WITH_ARGS(flatland, std::move(args), true);

  auto registered_presents = GetRegisteredPresents(flatland->GetRoot().GetInstanceId());
  EXPECT_TRUE(registered_presents.empty());

  EXPECT_NE(GetUberStruct(flatland.get()), nullptr);

  EXPECT_TRUE(utils::IsEventSignalled(release_copy, ZX_EVENT_SIGNALED));
}

TEST_F(FlatlandTest, PresentsUpdateInCallOrder) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Create an event to serve as the acquire fence for the first Present().
  PresentArgs args1;
  args1.acquire_fences = utils::CreateEventArray(1);
  auto acquire1_copy = utils::CopyEvent(args1.acquire_fences[0]);

  // Create an event to serve as a release fence.
  args1.release_fences = utils::CreateEventArray(1);
  auto release1_copy = utils::CopyEvent(args1.release_fences[0]);

  // Present, but do not signal the fence, and ensure Present is registered, the UberStructSystem is
  // empty, and the release fence is unsignaled.
  PRESENT_WITH_ARGS(flatland, std::move(args1), true);

  // No presents have been registered since the acquire fence hasn't been signaled yet.
  auto registered_presents = GetRegisteredPresents(flatland->GetRoot().GetInstanceId());
  EXPECT_EQ(registered_presents.size(), 0ul);
  EXPECT_EQ(GetUberStruct(flatland.get()), nullptr);
  EXPECT_FALSE(utils::IsEventSignalled(release1_copy, ZX_EVENT_SIGNALED));

  // Create a transform and make it the root.
  const TransformId kId = {1};

  flatland->CreateTransform(kId);
  flatland->SetRootTransform(kId);

  // Create another event to serve as the acquire fence for the second Present().
  PresentArgs args2;
  args2.acquire_fences = utils::CreateEventArray(1);
  auto acquire2_copy = utils::CopyEvent(args2.acquire_fences[0]);

  // Create an event to serve as a release fence.
  args2.release_fences = utils::CreateEventArray(1);
  auto release2_copy = utils::CopyEvent(args2.release_fences[0]);

  // Present, but do not signal the fence, and ensure there are two Presents registered, but the
  // UberStructSystem is still empty and both release fences are unsignaled.
  PRESENT_WITH_ARGS(flatland, std::move(args2), true);

  // No presents have been registered since neither of the acquire fences have been signaled yet.
  registered_presents = GetRegisteredPresents(flatland->GetRoot().GetInstanceId());
  EXPECT_EQ(registered_presents.size(), 0ul);
  EXPECT_EQ(GetUberStruct(flatland.get()), nullptr);
  EXPECT_FALSE(utils::IsEventSignalled(release1_copy, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(utils::IsEventSignalled(release2_copy, ZX_EVENT_SIGNALED));

  // Signal the fence for the second Present(). Since the first one is not done, there should still
  // be no Presents registered, no UberStruct for the instance, and neither fence should be
  // signaled.
  acquire2_copy.signal(0, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  ApplySessionUpdatesAndSignalFences();

  registered_presents = GetRegisteredPresents(flatland->GetRoot().GetInstanceId());
  EXPECT_EQ(registered_presents.size(), 0ul);

  EXPECT_EQ(GetUberStruct(flatland.get()), nullptr);

  EXPECT_FALSE(utils::IsEventSignalled(release1_copy, ZX_EVENT_SIGNALED));
  EXPECT_FALSE(utils::IsEventSignalled(release2_copy, ZX_EVENT_SIGNALED));

  // Signal the fence for the first Present(). This should trigger both Presents(), resulting no
  // registered Presents and an UberStruct with a 2-element topology: the local root, and kId.
  acquire1_copy.signal(0, ZX_EVENT_SIGNALED);

  EXPECT_CALL(*mock_flatland_presenter_, ScheduleUpdateForSession(_, _, _, _)).Times(2);
  RunLoopUntilIdle();

  ApplySessionUpdatesAndSignalFences();

  registered_presents = GetRegisteredPresents(flatland->GetRoot().GetInstanceId());
  EXPECT_TRUE(registered_presents.empty());

  auto uber_struct = GetUberStruct(flatland.get());
  EXPECT_NE(uber_struct, nullptr);
  EXPECT_EQ(uber_struct->local_topology.size(), 2ul);

  EXPECT_TRUE(utils::IsEventSignalled(release1_copy, ZX_EVENT_SIGNALED));
  EXPECT_TRUE(utils::IsEventSignalled(release2_copy, ZX_EVENT_SIGNALED));
}

TEST_F(FlatlandTest, SetHitRegionsErrorTest) {
  // Zero is not a valid transform ID.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    const TransformId kId = {0};
    flatland->SetHitRegions(kId, {});
    PRESENT(flatland, /*expect_success=*/false);
  }

  // Transform ID should be present.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    const TransformId kId = {42};
    flatland->SetHitRegions(kId, {});
    PRESENT(flatland, /*expect_success=*/false);
  }

  fuchsia::ui::composition::HitTestInteraction interaction =
      fuchsia::ui::composition::HitTestInteraction::DEFAULT;
  const TransformId kId = {1};

  // Height should be non-negative.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    fuchsia::math::RectF rect = {0, 0, 0, -1};
    fuchsia::ui::composition::HitRegion region = {rect, interaction};

    flatland->CreateTransform(kId);
    flatland->SetRootTransform(kId);
    flatland->SetHitRegions(kId, {region});
    PRESENT(flatland, /*expect_success=*/false);
  }

  // Width should be non-negative.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    fuchsia::math::RectF rect = {0, 0, -1, 0};
    fuchsia::ui::composition::HitRegion region = {rect, interaction};

    flatland->CreateTransform(kId);
    flatland->SetRootTransform(kId);
    flatland->SetHitRegions(kId, {region});
    PRESENT(flatland, /*expect_success=*/false);
  }

  // Negative origin should succeed.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    fuchsia::math::RectF rect = {-1, -1, 0, 0};
    fuchsia::ui::composition::HitRegion region = {rect, interaction};

    flatland->CreateTransform(kId);
    flatland->SetRootTransform(kId);
    flatland->SetHitRegions(kId, {region});
    PRESENT(flatland, /*expect_success=*/true);
  }

  // Empty hit region vector should succeed.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateTransform(kId);
    flatland->SetRootTransform(kId);
    flatland->SetHitRegions(kId, {});
    PRESENT(flatland, /*expect_success=*/true);
  }

  // Valid hit region vector should succeed.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    fuchsia::math::RectF rect = {0, 0, 10, 10};
    fuchsia::ui::composition::HitRegion region = {rect, interaction};

    flatland->CreateTransform(kId);
    flatland->SetRootTransform(kId);
    flatland->SetHitRegions(kId, {region});
    PRESENT(flatland, /*expect_success=*/true);
  }

  // Consecutive SetRootTransforms with the same transform should work.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    fuchsia::math::RectF rect = {0, 0, 0, 0};
    fuchsia::ui::composition::HitRegion region = {rect, interaction};

    flatland->CreateTransform(kId);
    flatland->SetRootTransform(kId);
    flatland->SetRootTransform(kId);
    flatland->SetHitRegions(kId, {region});
    PRESENT(flatland, /*expect_success=*/true);
  }
}

TEST_F(FlatlandTest, SetDebugNameAddsPrefixToLogs) {
  class TestErrorReporter : public scenic_impl::ErrorReporter {
   public:
    TestErrorReporter(std::optional<std::string>* last_error_log)
        : reported_error(last_error_log) {}

    std::optional<std::string>* reported_error = nullptr;

   private:
    // |scenic_impl::ErrorReporter|
    void ReportError(syslog::LogSeverity severity, std::string error_string) override {
      *reported_error = error_string;
    }
  };

  const TransformId kInvalidId = {0};

  // No prefix in errors by default.
  {
    std::optional<std::string> error_log;
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetErrorReporter(std::make_unique<TestErrorReporter>(&error_log));
    flatland->CreateTransform(kInvalidId);
    PRESENT(flatland, false);
    ASSERT_TRUE(error_log.has_value());
    EXPECT_EQ("CreateTransform called with transform_id 0", *error_log);
  }

  // SetDebugName() adds a prefix.
  {
    std::optional<std::string> error_log;
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetErrorReporter(std::make_unique<TestErrorReporter>(&error_log));
    flatland->SetDebugName("test_client");
    flatland->CreateTransform(kInvalidId);
    PRESENT(flatland, false);
    ASSERT_TRUE(error_log.has_value());
    EXPECT_EQ("Flatland client(test_client): CreateTransform called with transform_id 0",
              *error_log);
  }

  // ErrorReporter logs the prefix of multiple flatland clients correctly.
  {
    std::optional<std::string> error_log_a;
    std::optional<std::string> error_log_b;

    std::shared_ptr<Flatland> flatland_a = CreateFlatland();
    std::shared_ptr<Flatland> flatland_b = CreateFlatland();

    flatland_a->SetErrorReporter(std::make_unique<TestErrorReporter>(&error_log_a));
    flatland_b->SetErrorReporter(std::make_unique<TestErrorReporter>(&error_log_b));

    flatland_a->SetDebugName("test_client");
    flatland_b->SetDebugName("test_client1");

    flatland_a->CreateTransform(kInvalidId);
    flatland_b->CreateTransform(kInvalidId);

    PRESENT(flatland_a, false);
    PRESENT(flatland_b, false);

    ASSERT_TRUE(error_log_a.has_value());
    ASSERT_TRUE(error_log_b.has_value());

    EXPECT_EQ("Flatland client(test_client): CreateTransform called with transform_id 0",
              *error_log_a);
    EXPECT_EQ("Flatland client(test_client1): CreateTransform called with transform_id 0",
              *error_log_b);
  }
}

TEST_F(FlatlandTest, SetDebugNameAddsDebugNameToUberStruct) {
  // debug_name is empty when SetDebugName() is not called.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    PRESENT(flatland, true);
    auto uber_struct = GetUberStruct(flatland.get());
    EXPECT_TRUE(uber_struct->debug_name.empty());
  }
  // UberStruct sees value of SetDebugName() after Present().
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetDebugName("test_client");
    PRESENT(flatland, true);
    auto uber_struct = GetUberStruct(flatland.get());
    EXPECT_EQ("test_client", uber_struct->debug_name);
  }
  // Clear() resets the debug_name member of UberStruct.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetDebugName("test_client");
    PRESENT(flatland, true);
    auto uber_struct = GetUberStruct(flatland.get());
    EXPECT_EQ("test_client", uber_struct->debug_name);
    flatland->Clear();
    PRESENT(flatland, true);
    uber_struct = GetUberStruct(flatland.get());
    EXPECT_TRUE(uber_struct->debug_name.empty());
  }
}

TEST_F(FlatlandTest, CreateAndReleaseTransformValidCases) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  const TransformId kId1 = {1};
  const TransformId kId2 = {2};

  // Create two transforms.
  flatland->CreateTransform(kId1);
  flatland->CreateTransform(kId2);
  PRESENT(flatland, true);

  // Clear, then create two transforms in the other order.
  flatland->Clear();
  flatland->CreateTransform(kId2);
  flatland->CreateTransform(kId1);
  PRESENT(flatland, true);

  // Clear, create and release transforms, non-overlapping.
  flatland->Clear();
  flatland->CreateTransform(kId1);
  flatland->ReleaseTransform(kId1);
  flatland->CreateTransform(kId2);
  flatland->ReleaseTransform(kId2);
  PRESENT(flatland, true);

  // Clear, create and release transforms, nested.
  flatland->Clear();
  flatland->CreateTransform(kId2);
  flatland->CreateTransform(kId1);
  flatland->ReleaseTransform(kId1);
  flatland->ReleaseTransform(kId2);
  PRESENT(flatland, true);

  // Reuse the same id, legally, in a single present call.
  flatland->CreateTransform(kId1);
  flatland->ReleaseTransform(kId1);
  flatland->CreateTransform(kId1);
  flatland->Clear();
  flatland->CreateTransform(kId1);
  PRESENT(flatland, true);

  // Create and clear, overlapping, with multiple present calls.
  flatland->Clear();
  flatland->CreateTransform(kId2);
  PRESENT(flatland, true);
  flatland->CreateTransform(kId1);
  flatland->ReleaseTransform(kId2);
  PRESENT(flatland, true);
  flatland->ReleaseTransform(kId1);
  PRESENT(flatland, true);
}

TEST_F(FlatlandTest, CreateAndReleaseTransformErrorCases) {
  const TransformId kId1 = {1};
  const TransformId kId2 = {2};

  // Zero is not a valid transform id.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateTransform({0});
    PRESENT(flatland, false);
  }
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->ReleaseTransform({0});
    PRESENT(flatland, false);
  }

  // Double creation is an error.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateTransform(kId1);
    flatland->CreateTransform(kId1);
    PRESENT(flatland, false);
  }

  // Releasing a non-existent transform is an error.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->ReleaseTransform(kId2);
    PRESENT(flatland, false);
  }
}

TEST_F(FlatlandTest, AddAndRemoveChildValidCases) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  const TransformId kIdParent = {1};
  const TransformId kIdChild1 = {2};
  const TransformId kIdChild2 = {3};
  const TransformId kIdGrandchild = {4};

  flatland->CreateTransform(kIdParent);
  flatland->CreateTransform(kIdChild1);
  flatland->CreateTransform(kIdChild2);
  flatland->CreateTransform(kIdGrandchild);
  PRESENT(flatland, true);

  // Add and remove.
  flatland->AddChild(kIdParent, kIdChild1);
  flatland->RemoveChild(kIdParent, kIdChild1);
  PRESENT(flatland, true);

  // Add two children.
  flatland->AddChild(kIdParent, kIdChild1);
  flatland->AddChild(kIdParent, kIdChild2);
  PRESENT(flatland, true);

  // Remove two children.
  flatland->RemoveChild(kIdParent, kIdChild1);
  flatland->RemoveChild(kIdParent, kIdChild2);
  PRESENT(flatland, true);

  // Add two-deep hierarchy.
  flatland->AddChild(kIdParent, kIdChild1);
  flatland->AddChild(kIdChild1, kIdGrandchild);
  PRESENT(flatland, true);

  // Add sibling.
  flatland->AddChild(kIdParent, kIdChild2);
  PRESENT(flatland, true);

  // Add shared grandchild (deadly diamond dependency).
  flatland->AddChild(kIdChild2, kIdGrandchild);
  PRESENT(flatland, true);

  // Remove original deep-hierarchy.
  flatland->RemoveChild(kIdChild1, kIdGrandchild);
  PRESENT(flatland, true);
}

TEST_F(FlatlandTest, AddAndRemoveChildErrorCases) {
  const TransformId kIdParent = {1};
  const TransformId kIdChild = {2};
  const TransformId kIdNotCreated = {3};

  // Setup.
  auto SetupFlatland = [&]() {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateTransform(kIdParent);
    flatland->CreateTransform(kIdChild);
    flatland->AddChild(kIdParent, kIdChild);
    return flatland;
  };

  {
    auto flatland = SetupFlatland();
    PRESENT(flatland, true);
  }

  // Zero is not a valid transform id.
  {
    auto flatland = SetupFlatland();
    flatland->AddChild({0}, {0});
    PRESENT(flatland, false);
  }
  {
    auto flatland = SetupFlatland();
    flatland->AddChild(kIdParent, {0});
    PRESENT(flatland, false);
  }
  {
    auto flatland = SetupFlatland();
    flatland->AddChild({0}, kIdChild);
    PRESENT(flatland, false);
  }

  // Child does not exist.
  {
    auto flatland = SetupFlatland();
    flatland->AddChild(kIdParent, kIdNotCreated);
    PRESENT(flatland, false);
  }
  {
    auto flatland = SetupFlatland();
    flatland->RemoveChild(kIdParent, kIdNotCreated);
    PRESENT(flatland, false);
  }

  // Parent does not exist.
  {
    auto flatland = SetupFlatland();
    flatland->AddChild(kIdNotCreated, kIdChild);
    PRESENT(flatland, false);
  }
  {
    auto flatland = SetupFlatland();
    flatland->RemoveChild(kIdNotCreated, kIdChild);
    PRESENT(flatland, false);
  }

  // Child is already a child of parent->
  {
    auto flatland = SetupFlatland();
    flatland->AddChild(kIdParent, kIdChild);
    PRESENT(flatland, false);
  }

  // Both nodes exist, but not in the correct relationship.
  {
    auto flatland = SetupFlatland();
    flatland->RemoveChild(kIdChild, kIdParent);
    PRESENT(flatland, false);
  }
}

// Test that Transforms can be children to multiple different parents.
TEST_F(FlatlandTest, MultichildUsecase) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  const TransformId kIdParent1 = {1};
  const TransformId kIdParent2 = {2};
  const TransformId kIdChild1 = {3};
  const TransformId kIdChild2 = {4};
  const TransformId kIdChild3 = {5};

  // Setup
  flatland->CreateTransform(kIdParent1);
  flatland->CreateTransform(kIdParent2);
  flatland->CreateTransform(kIdChild1);
  flatland->CreateTransform(kIdChild2);
  flatland->CreateTransform(kIdChild3);
  PRESENT(flatland, true);

  // Add all children to first parent->
  flatland->AddChild(kIdParent1, kIdChild1);
  flatland->AddChild(kIdParent1, kIdChild2);
  flatland->AddChild(kIdParent1, kIdChild3);
  PRESENT(flatland, true);

  // Add all children to second parent->
  flatland->AddChild(kIdParent2, kIdChild1);
  flatland->AddChild(kIdParent2, kIdChild2);
  flatland->AddChild(kIdParent2, kIdChild3);
  PRESENT(flatland, true);
}

// Test that when a transform has multiple parents, that
// the number of nodes in the uber_structs global topology
// is what we would expect.
TEST_F(FlatlandTest, MultichildTest2) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  const TransformId kIdRoot = {1};
  const TransformId kIdParent1 = {2};
  const TransformId kIdParent2 = {3};
  const TransformId kIdChild = {4};

  // Create the transforms.
  flatland->CreateTransform(kIdRoot);
  flatland->CreateTransform(kIdParent1);
  flatland->CreateTransform(kIdParent2);
  flatland->CreateTransform(kIdChild);
  PRESENT(flatland, true);

  // Setup the diamond parent hierarchy.
  flatland->SetRootTransform(kIdRoot);
  flatland->AddChild(kIdRoot, kIdParent1);
  flatland->AddChild(kIdRoot, kIdParent2);
  flatland->AddChild(kIdParent1, kIdChild);
  flatland->AddChild(kIdParent2, kIdChild);
  PRESENT(flatland, true);

  // The transform kIdChild should be doubly listed
  // in the uber struct.
  auto uber_struct = GetUberStruct(flatland.get());
  EXPECT_EQ(uber_struct->local_topology.size(), 6U);
}

// Test that Present() fails if it detects a graph cycle.
TEST_F(FlatlandTest, CycleDetector) {
  const TransformId kId1 = {1};
  const TransformId kId2 = {2};
  const TransformId kId3 = {3};
  const TransformId kId4 = {4};

  // Create an immediate cycle.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateTransform(kId1);
    flatland->AddChild(kId1, kId1);
    PRESENT(flatland, false);
  }

  // Create a legal chain of depth one.
  // Then, create a cycle of length 2.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->Clear();
    flatland->CreateTransform(kId1);
    flatland->CreateTransform(kId2);
    flatland->AddChild(kId1, kId2);
    PRESENT(flatland, true);

    flatland->AddChild(kId2, kId1);
    PRESENT(flatland, false);
  }

  // Create two legal chains of length one.
  // Then, connect each chain into a cycle of length four.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->Clear();
    flatland->CreateTransform(kId1);
    flatland->CreateTransform(kId2);
    flatland->CreateTransform(kId3);
    flatland->CreateTransform(kId4);
    flatland->AddChild(kId1, kId2);
    flatland->AddChild(kId3, kId4);
    PRESENT(flatland, true);

    flatland->AddChild(kId2, kId3);
    flatland->AddChild(kId4, kId1);
    PRESENT(flatland, false);
  }

  // Create a cycle, where the root is not involved in the cycle.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->Clear();
    flatland->CreateTransform(kId1);
    flatland->CreateTransform(kId2);
    flatland->CreateTransform(kId3);
    flatland->CreateTransform(kId4);

    flatland->AddChild(kId1, kId2);
    flatland->AddChild(kId2, kId3);
    flatland->AddChild(kId3, kId2);
    flatland->AddChild(kId3, kId4);

    flatland->SetRootTransform(kId1);
    flatland->ReleaseTransform(kId1);
    flatland->ReleaseTransform(kId2);
    flatland->ReleaseTransform(kId3);
    flatland->ReleaseTransform(kId4);
    PRESENT(flatland, false);
  }
}

TEST_F(FlatlandTest, SetRootTransform) {
  const TransformId kId1 = {1};
  const TransformId kIdNotCreated = {2};

  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateTransform(kId1);
    PRESENT(flatland, true);

    // Even with no root transform, so clearing it is not an error.
    flatland->SetRootTransform({0});
    PRESENT(flatland, true);

    flatland->SetRootTransform(kId1);
    PRESENT(flatland, true);

    // Setting the root to a non-existent transform does not clear the root, which means the local
    // topology will contain two handles: the "local root" and kId1.
    auto uber_struct = GetUberStruct(flatland.get());
    EXPECT_EQ(uber_struct->local_topology.size(), 2ul);

    // Releasing the root is allowed, though it will remain in the hierarchy until reset.
    flatland->ReleaseTransform(kId1);
    PRESENT(flatland, true);

    // Clearing the root after release is also allowed.
    flatland->SetRootTransform({0});
    PRESENT(flatland, true);

    // Setting the root to a released transform is not allowed.
    flatland->SetRootTransform(kId1);
    PRESENT(flatland, false);
  }

  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetRootTransform(kIdNotCreated);
    PRESENT(flatland, false);
  }
}

TEST_F(FlatlandTest, SetTranslationErrorCases) {
  const TransformId kIdNotCreated = {1};

  // Zero is not a valid transform ID.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetTranslation({0}, {1, 2});
    PRESENT(flatland, false);
  }

  // Transform does not exist.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetTranslation(kIdNotCreated, {1, 2});
    PRESENT(flatland, false);
  }
}

TEST_F(FlatlandTest, SetOrientationErrorCases) {
  const TransformId kIdNotCreated = {1};

  // Zero is not a valid transform ID.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetOrientation({0}, Orientation::CCW_90_DEGREES);
    PRESENT(flatland, false);
  }

  // Transform does not exist.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetOrientation(kIdNotCreated, Orientation::CCW_90_DEGREES);
    PRESENT(flatland, false);
  }
}

TEST_F(FlatlandTest, SetScaleErrorCases) {
  const TransformId kIdNotCreated = {1};

  // Zero is not a valid transform ID.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetScale({0}, {1, 2});
    PRESENT(flatland, false);
  }

  // Transform does not exist.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetScale(kIdNotCreated, {1, 2});
    PRESENT(flatland, false);
  }
}

TEST_F(FlatlandTest, SetImageDestinationSizeErrorCases) {
  const ContentId kIdNotCreated = {1};

  // Zero is not a valid content ID.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetImageDestinationSize({0}, {1, 2});
    PRESENT(flatland, false);
  }

  // Content does not exist.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetImageDestinationSize(kIdNotCreated, {1, 2});
    PRESENT(flatland, false);
  }
}

TEST_F(FlatlandTest, SetImageBlendFunctionErrorCases) {
  const ContentId kIdNotCreated = {1};

  // Zero is not a valid content ID.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetImageBlendingFunction({0}, fuchsia::ui::composition::BlendMode::SRC);
    PRESENT(flatland, false);
  }

  // Content does not exist.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetImageBlendingFunction(kIdNotCreated, fuchsia::ui::composition::BlendMode::SRC);
    PRESENT(flatland, false);
  }
}

// Make sure that the data for setting the blend mode gets passed to
// the uberstruct correctly.
TEST_F(FlatlandTest, SetImageBlendFunctionUberstructTest) {
  const ContentId kImageId1 = {1};
  const ContentId kImageId2 = {2};
  const TransformId kTransformId1 = {3};
  const TransformId kTransformId2 = {4};

  std::shared_ptr<Flatland> flatland = CreateFlatland();
  std::shared_ptr<Allocator> allocator = CreateAllocator();

  // Create constants.
  const uint32_t kImageWidth = 50;
  const uint32_t kImageHeight = 100;

  // Setup first image to be opaque.
  {
    BufferCollectionImportExportTokens ref_pair_1 = BufferCollectionImportExportTokens::New();

    ImageProperties properties1;
    properties1.set_size({kImageWidth, kImageHeight});

    auto import_token_dup = ref_pair_1.DuplicateImportToken();
    const allocation::GlobalBufferCollectionId global_collection_id1 =
        CreateImage(flatland.get(), allocator.get(), kImageId1, std::move(ref_pair_1),
                    std::move(properties1))
            .collection_id;

    flatland->CreateTransform(kTransformId1);
    flatland->SetRootTransform(kTransformId1);
    flatland->SetContent(kTransformId1, kImageId1);
    flatland->SetImageBlendingFunction(kImageId1, fuchsia::ui::composition::BlendMode::SRC);
    PRESENT(flatland, true);
  }

  // Create a second image to be transparent.
  {
    BufferCollectionImportExportTokens ref_pair_1 = BufferCollectionImportExportTokens::New();

    ImageProperties properties1;
    properties1.set_size({kImageWidth, kImageHeight});

    auto import_token_dup = ref_pair_1.DuplicateImportToken();
    const allocation::GlobalBufferCollectionId global_collection_id1 =
        CreateImage(flatland.get(), allocator.get(), kImageId2, std::move(ref_pair_1),
                    std::move(properties1))
            .collection_id;

    flatland->CreateTransform(kTransformId2);
    flatland->AddChild(kTransformId1, kTransformId2);
    flatland->SetContent(kTransformId2, kImageId2);
    flatland->SetImageBlendingFunction(kImageId2, fuchsia::ui::composition::BlendMode::SRC_OVER);
    PRESENT(flatland, true);
  }

  // Get the first image content handle
  const auto maybe_image_1_handle = flatland->GetContentHandle(kImageId1);
  ASSERT_TRUE(maybe_image_1_handle.has_value());
  const auto image_1_handle = maybe_image_1_handle.value();

  // Get the second image content handle
  const auto maybe_image_2_handle = flatland->GetContentHandle(kImageId2);
  ASSERT_TRUE(maybe_image_2_handle.has_value());
  const auto image_2_handle = maybe_image_2_handle.value();

  // Now find the data in the uber struct.
  auto uber_struct = GetUberStruct(flatland.get());
  EXPECT_EQ(uber_struct->local_topology.back().handle, image_2_handle);

  // Grab the metadatas for each handle.
  auto image_1_kv = uber_struct->images.find(image_1_handle);
  EXPECT_NE(image_1_kv, uber_struct->images.end());

  auto image_2_kv = uber_struct->images.find(image_2_handle);
  EXPECT_NE(image_2_kv, uber_struct->images.end());

  // Make sure the opacity fields are set properly.
  EXPECT_TRUE(image_1_kv->second.blend_mode == fuchsia::ui::composition::BlendMode::SRC);
  EXPECT_TRUE(image_2_kv->second.blend_mode == fuchsia::ui::composition::BlendMode::SRC_OVER);
}

// Test that changing geometric transform properties affects the local matrix of Transforms.
TEST_F(FlatlandTest, SetGeometricTransformProperties) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Create two Transforms to ensure properties are local to individual Transforms.
  const TransformId kId1 = {1};
  const TransformId kId2 = {2};

  flatland->CreateTransform(kId1);
  flatland->CreateTransform(kId2);

  flatland->SetRootTransform(kId1);
  flatland->AddChild(kId1, kId2);

  PRESENT(flatland, true);

  // Get the TransformHandles for kId1 and kId2.
  auto uber_struct = GetUberStruct(flatland.get());
  ASSERT_EQ(uber_struct->local_topology.size(), 3ul);
  ASSERT_EQ(uber_struct->local_topology[0].handle, flatland->GetRoot());

  const auto handle1 = uber_struct->local_topology[1].handle;
  const auto handle2 = uber_struct->local_topology[2].handle;

  // The local topology will always have 3 transforms: the local root, kId1, and kId2. With no
  // properties set, there will be no local matrices.
  uber_struct = GetUberStruct(flatland.get());
  EXPECT_TRUE(uber_struct->local_matrices.empty());

  // Set one transform property for each node.
  // Set translation on first transform.
  flatland->SetTranslation(kId1, {1, 2});

  // Set scale on second transform.
  flatland->SetScale(kId2, {2.f, 3.f});
  PRESENT(flatland, true);

  // The two handles should have the expected matrices.
  uber_struct = GetUberStruct(flatland.get());
  EXPECT_MATRIX(uber_struct, handle1, glm::translate(glm::mat3(), {1, 2}));
  EXPECT_MATRIX(uber_struct, handle2, glm::scale(glm::mat3(), {2.f, 3.f}));

  // Fill out the remaining properties on both transforms.
  flatland->SetScale(kId1, {4.f, 5.f});
  flatland->SetOrientation(kId1, Orientation::CCW_90_DEGREES);

  flatland->SetOrientation(kId2, Orientation::CCW_270_DEGREES);
  flatland->SetTranslation(kId2, {6, 7});

  PRESENT(flatland, true);

  // Verify the new properties were applied in the correct orders.
  uber_struct = GetUberStruct(flatland.get());

  // The way the glm function calls handle translation/scale/rotation is
  // a bit unintuitive. If you call glm::scale(translation_matrix, vec2),
  // this may appear as if we are incorrectly translating first and scaling
  // second, but glm will apply them in the order (translation * scale * (point))
  // which is indeed the ordering that we want. In other words, the built-in glm
  // calls *right multiply* instead of *left multiply*.
  glm::mat3 matrix1 = glm::mat3();
  matrix1 = glm::translate(matrix1, {1, 2});
  matrix1 = glm::rotate(matrix1, utils::GetOrientationAngle(Orientation::CCW_90_DEGREES));
  matrix1 = glm::scale(matrix1, {4.f, 5.f});
  EXPECT_MATRIX(uber_struct, handle1, matrix1);

  glm::mat3 matrix2 = glm::mat3();
  matrix2 = glm::translate(matrix2, {6, 7});
  matrix2 = glm::rotate(matrix2, utils::GetOrientationAngle(Orientation::CCW_270_DEGREES));
  matrix2 = glm::scale(matrix2, {2.f, 3.f});
  EXPECT_MATRIX(uber_struct, handle2, matrix2);
}

// Ensure that local matrix data is only cleaned up when a Transform is completely unreferenced,
// meaning no Transforms reference it as a child->
TEST_F(FlatlandTest, MatrixReleasesWhenTransformNotReferenced) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Create two Transforms to ensure properties are local to individual Transforms.
  const TransformId kId1 = {1};
  const TransformId kId2 = {2};

  flatland->CreateTransform(kId1);
  flatland->CreateTransform(kId2);

  flatland->SetRootTransform(kId1);
  flatland->AddChild(kId1, kId2);

  PRESENT(flatland, true);

  // Get the TransformHandles for kId1 and kId2.
  auto uber_struct = GetUberStruct(flatland.get());
  ASSERT_EQ(uber_struct->local_topology.size(), 3ul);
  ASSERT_EQ(uber_struct->local_topology[0].handle, flatland->GetRoot());

  const auto handle1 = uber_struct->local_topology[1].handle;
  const auto handle2 = uber_struct->local_topology[2].handle;

  // Set a geometric property on kId1.
  flatland->SetTranslation(kId1, {1, 2});
  PRESENT(flatland, true);

  // Only handle1 should have a local matrix.
  uber_struct = GetUberStruct(flatland.get());
  EXPECT_MATRIX(uber_struct, handle1, glm::translate(glm::mat3(), {1, 2}));

  // Release kId1, but ensure its matrix stays around.
  flatland->ReleaseTransform(kId1);
  PRESENT(flatland, true);

  uber_struct = GetUberStruct(flatland.get());
  EXPECT_MATRIX(uber_struct, handle1, glm::translate(glm::mat3(), {1, 2}));

  // Clear kId1 as the root transform, which should clear the matrix.
  flatland->SetRootTransform({0});
  PRESENT(flatland, true);

  uber_struct = GetUberStruct(flatland.get());
  EXPECT_TRUE(uber_struct->local_matrices.empty());
}

TEST_F(FlatlandTest, CreateViewReplaceWithoutConnection) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  flatland->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(),
                        NoViewProtocols(), parent_viewport_watcher.NewRequest());
  PRESENT(flatland, true);

  ViewportCreationToken parent_token2;
  ViewCreationToken child_token2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token2.value, &child_token2.value));

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher2;
  flatland->CreateView2(std::move(child_token2), scenic::NewViewIdentityOnCreation(),
                        NoViewProtocols(), parent_viewport_watcher2.NewRequest());

  RunLoopUntilIdle();

  // Until Present() is called, the previous ParentViewportWatcher is not unbound.
  EXPECT_TRUE(parent_viewport_watcher.is_bound());
  EXPECT_TRUE(parent_viewport_watcher2.is_bound());

  PRESENT(flatland, true);

  EXPECT_FALSE(parent_viewport_watcher.is_bound());
  EXPECT_TRUE(parent_viewport_watcher2.is_bound());
}

TEST_F(FlatlandTest, ParentViewportWatcherReplaceWithConnection) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  const ContentId kLinkId1 = {1};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  CreateViewport(parent.get(), child.get(), kLinkId1, &child_view_watcher,
                 &parent_viewport_watcher);

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher2;

  // Don't use the helper function for the second link to test when the previous links are closed.
  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  // Creating the new ParentViewportWatcher doesn't invalidate either of the old links until
  // Present() is called on the child->
  child->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(), NoViewProtocols(),
                     parent_viewport_watcher2.NewRequest());

  RunLoopUntilIdle();

  EXPECT_TRUE(child_view_watcher.is_bound());
  EXPECT_TRUE(parent_viewport_watcher.is_bound());
  EXPECT_TRUE(parent_viewport_watcher2.is_bound());

  // Present() replaces the original ParentViewportWatcher, which also results in the invalidation
  // of both ends of the original link.
  PRESENT(child, true);

  EXPECT_FALSE(child_view_watcher.is_bound());
  EXPECT_FALSE(parent_viewport_watcher.is_bound());
  EXPECT_TRUE(parent_viewport_watcher2.is_bound());
}

TEST_F(FlatlandTest, ParentViewportWatcherUnbindsOnParentDeath) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  flatland->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(),
                        NoViewProtocols(), parent_viewport_watcher.NewRequest());
  PRESENT(flatland, true);

  parent_token.value.reset();
  RunLoopUntilIdle();

  EXPECT_FALSE(parent_viewport_watcher.is_bound());
}

TEST_F(FlatlandTest, ParentViewportWatcherUnbindsImmediatelyWithInvalidToken) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  ViewCreationToken child_token;

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  flatland->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(),
                        NoViewProtocols(), parent_viewport_watcher.NewRequest());

  // The link will be unbound even before Present() is called.
  RunLoopUntilIdle();
  EXPECT_FALSE(parent_viewport_watcher.is_bound());

  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, ReleaseViewFailsWithoutLink) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  flatland->ReleaseView();

  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, ReleaseViewSucceedsWithLink) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  flatland->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(),
                        NoViewProtocols(), parent_viewport_watcher.NewRequest());
  PRESENT(flatland, true);

  // Killing the peer token does not prevent the instance from releasing view.
  parent_token.value.reset();
  RunLoopUntilIdle();

  flatland->ReleaseView();
  PRESENT(flatland, true);
}

TEST_F(FlatlandTest, CreateViewSuccceedsAfterReleaseView) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  flatland->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(),
                        NoViewProtocols(), parent_viewport_watcher.NewRequest());
  PRESENT(flatland, true);

  // Killing the peer token does not prevent the instance from releasing view.
  parent_token.value.reset();
  RunLoopUntilIdle();

  ViewportCreationToken parent_token2;
  ViewCreationToken child_token2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token2.value, &child_token2.value));

  flatland->CreateView2(std::move(child_token2), scenic::NewViewIdentityOnCreation(),
                        NoViewProtocols(), parent_viewport_watcher.NewRequest());
  PRESENT(flatland, true);
}

// TODO(fxb/81576): Reenable.
TEST_F(FlatlandTest, DISABLED_GraphUnlinkReturnsOrphanedTokenOnParentDeath) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  flatland->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(),
                        NoViewProtocols(), parent_viewport_watcher.NewRequest());
  PRESENT(flatland, true);

  // Killing the peer token does not prevent the instance from returning a valid token.
  parent_token.value.reset();
  RunLoopUntilIdle();

  ViewCreationToken view_creation_token;
  // flatland->ReleaseView(
  //     [&view_creation_token](ViewCreationToken token) { view_creation_token = std::move(token);
  //     });
  PRESENT(flatland, true);

  EXPECT_TRUE(view_creation_token.value.is_valid());

  // But trying to link with that token will immediately fail because it is already orphaned.
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher2;
  flatland->CreateView2(std::move(view_creation_token), scenic::NewViewIdentityOnCreation(),
                        NoViewProtocols(), parent_viewport_watcher2.NewRequest());
  PRESENT(flatland, true);

  EXPECT_FALSE(parent_viewport_watcher2.is_bound());
}

// TODO(fxb/81576): Reenable.
TEST_F(FlatlandTest, DISABLED_GraphUnlinkReturnsOriginalToken) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  const zx_koid_t expected_koid = fsl::GetKoid(child_token.value.get());

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  flatland->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(),
                        NoViewProtocols(), parent_viewport_watcher.NewRequest());
  PRESENT(flatland, true);

  ViewCreationToken view_creation_token;
  // flatland->ReleaseView(
  //     [&view_creation_token](ViewCreationToken token) { view_creation_token = std::move(token);
  //     });

  RunLoopUntilIdle();

  // Until Present() is called and the acquire fence is signaled, the previous ParentViewportWatcher
  // is not unbound.
  EXPECT_TRUE(parent_viewport_watcher.is_bound());
  EXPECT_FALSE(view_creation_token.value.is_valid());

  PresentArgs args;
  args.acquire_fences = utils::CreateEventArray(1);
  auto event_copy = utils::CopyEvent(args.acquire_fences[0]);

  PRESENT_WITH_ARGS(flatland, std::move(args), true);

  EXPECT_TRUE(parent_viewport_watcher.is_bound());
  EXPECT_FALSE(view_creation_token.value.is_valid());

  // Signal the acquire fence to unbind the link.
  event_copy.signal(0, ZX_EVENT_SIGNALED);

  EXPECT_CALL(*mock_flatland_presenter_, ScheduleUpdateForSession(_, _, _, _));
  RunLoopUntilIdle();

  EXPECT_FALSE(parent_viewport_watcher.is_bound());
  EXPECT_TRUE(view_creation_token.value.is_valid());
  EXPECT_EQ(fsl::GetKoid(view_creation_token.value.get()), expected_koid);
}

TEST_F(FlatlandTest, ChildViewWatcherUnbindsOnChildDeath) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  const ContentId kLinkId1 = {1};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  flatland->CreateViewport(kLinkId1, std::move(parent_token), std::move(properties),
                           child_view_watcher.NewRequest());
  PRESENT(flatland, true);

  child_token.value.reset();
  RunLoopUntilIdle();

  EXPECT_FALSE(child_view_watcher.is_bound());
}

TEST_F(FlatlandTest, ChildViewWatcherUnbindsImmediatelyWithInvalidToken) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  ViewportCreationToken parent_token;

  const ContentId kLinkId1 = {1};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  flatland->CreateViewport(kLinkId1, std::move(parent_token), {}, child_view_watcher.NewRequest());

  // The link will be unbound even before Present() is called.
  RunLoopUntilIdle();
  EXPECT_FALSE(child_view_watcher.is_bound());

  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, ChildViewWatcherFailsIdIsZero) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  flatland->CreateViewport({0}, std::move(parent_token), std::move(properties),
                           child_view_watcher.NewRequest());
  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, ChildViewWatcherFailsNoLogicalSize) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  flatland->CreateViewport({0}, std::move(parent_token), std::move(properties),
                           child_view_watcher.NewRequest());
  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, ChildViewWatcherFailsInvalidLogicalSize) {
  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;

  // The X value must be positive.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));
    ViewportProperties properties;
    properties.set_logical_size({0, kDefaultSize});
    flatland->CreateViewport({0}, std::move(parent_token), std::move(properties),
                             child_view_watcher.NewRequest());
    PRESENT(flatland, false);
  }

  // The Y value must be positive.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));
    ViewportProperties properties2;
    properties2.set_logical_size({kDefaultSize, 0});
    flatland->CreateViewport({0}, std::move(parent_token), std::move(properties2),
                             child_view_watcher.NewRequest());
    PRESENT(flatland, false);
  }
}

TEST_F(FlatlandTest, ChildViewAutomaticallyClipsBounds) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  const ContentId kLinkId1 = {1};
  ViewportProperties properties;
  // Create the viewport and check the uberstruct for the clip bounds.
  {
    const int32_t kWidth = 300;
    const int32_t kHeight = 500;
    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
    properties.set_logical_size({kWidth, kHeight});
    flatland->CreateViewport(kLinkId1, std::move(parent_token), std::move(properties),
                             child_view_watcher.NewRequest());
    PRESENT(flatland, true);

    auto maybe_transform = flatland->GetContentHandle(kLinkId1);
    EXPECT_TRUE(maybe_transform);
    auto transform = *maybe_transform;

    auto uber_struct = GetUberStruct(flatland.get());
    auto clip_itr = uber_struct->local_clip_regions.find(transform);
    EXPECT_NE(clip_itr, uber_struct->local_clip_regions.end());

    auto clip_region = clip_itr->second;
    EXPECT_EQ(clip_region.x, 0);
    EXPECT_EQ(clip_region.y, 0);
    EXPECT_EQ(clip_region.width, kWidth);
    EXPECT_EQ(clip_region.height, kHeight);
  }

  // Change the bounds via a call to |SetViewProperties| and make sure they've changed.
  {
    const int32_t kWidth = 900;
    const int32_t kHeight = 700;
    properties.set_logical_size({kWidth, kHeight});
    flatland->SetViewportProperties(kLinkId1, std::move(properties));
    PRESENT(flatland, true);

    auto maybe_transform = flatland->GetContentHandle(kLinkId1);
    EXPECT_TRUE(maybe_transform);
    auto transform = *maybe_transform;

    auto uber_struct = GetUberStruct(flatland.get());
    auto clip_itr = uber_struct->local_clip_regions.find(transform);
    EXPECT_NE(clip_itr, uber_struct->local_clip_regions.end());

    auto clip_region = clip_itr->second;
    EXPECT_EQ(clip_region.x, 0);
    EXPECT_EQ(clip_region.y, 0);
    EXPECT_EQ(clip_region.width, kWidth);
    EXPECT_EQ(clip_region.height, kHeight);
  }
}

TEST_F(FlatlandTest, ViewportClippingPersistsAcrossInstances) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  // Create and link the two instances.
  const TransformId kId1 = {1};
  parent->CreateTransform(kId1);
  parent->SetRootTransform(kId1);

  const ContentId kLinkId = {1};

  fidl::InterfacePtr<ChildViewWatcher> parent_child_view_watcher;
  ViewportProperties properties;
  const int32_t kViewportWidth = 75;
  const int32_t kViewportHeight = 325;
  properties.set_logical_size({kViewportWidth, kViewportHeight});
  parent->CreateViewport(kLinkId, std::move(parent_token), std::move(properties),
                         parent_child_view_watcher.NewRequest());
  parent->SetContent(kId1, kLinkId);

  fidl::InterfacePtr<ParentViewportWatcher> child_parent_viewport_watcher;
  child->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(), NoViewProtocols(),
                     child_parent_viewport_watcher.NewRequest());

  PRESENT(parent, true);
  PRESENT(child, true);

  EXPECT_TRUE(IsDescendantOf(parent->GetRoot(), child->GetRoot()));

  // Calculate the global clip regions of the parent and child flatland instances, and ensure
  // that the root of the child instance has a global clip region equivalent to that of the
  // logical size of the parent viewport's properties.
  const auto snapshot = uber_struct_system_->Snapshot();
  const auto links = link_system_->GetResolvedTopologyLinks();
  const auto link_system_id = link_system_->GetInstanceId();

  const auto topology_data = flatland::GlobalTopologyData::ComputeGlobalTopologyData(
      snapshot, links, link_system_id, parent->GetRoot());
  const auto global_matrices = flatland::ComputeGlobalMatrices(
      topology_data.topology_vector, topology_data.parent_indices, snapshot);

  const auto global_clip_regions = ComputeGlobalTransformClipRegions(
      topology_data.topology_vector, topology_data.parent_indices, global_matrices, snapshot);

  auto child_root_handle = topology_data.topology_vector[3];
  EXPECT_EQ(child->GetRoot(), child_root_handle);
  auto child_root_clip = global_clip_regions[3];
  EXPECT_EQ(child_root_clip.x, 0);
  EXPECT_EQ(child_root_clip.x, 0);
  EXPECT_EQ(child_root_clip.width, kViewportWidth);
  EXPECT_EQ(child_root_clip.height, kViewportHeight);
}

TEST_F(FlatlandTest, DefaultHitRegionsExist_OnlyForCurrentRoot) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();
  const auto session_id = flatland->GetSessionId();

  const TransformId kId1 = {1};
  const TransformHandle handle1 = TransformHandle(session_id, 1);

  const TransformId kId2 = {2};
  const TransformHandle handle2 = TransformHandle(session_id, 2);

  flatland->CreateTransform(kId1);
  flatland->CreateTransform(kId2);

  flatland->SetRootTransform(kId1);

  PRESENT(flatland, true);
  {
    auto uber_struct = GetUberStruct(flatland.get());
    auto& hit_regions = uber_struct->local_hit_regions_map;

    EXPECT_EQ(hit_regions.size(), 1u);
    EXPECT_EQ(hit_regions[handle1].size(), 1u);
    EXPECT_EQ(hit_regions[handle2].size(), 0u);
  }

  flatland->SetRootTransform(kId2);

  PRESENT(flatland, true);

  {
    auto uber_struct = GetUberStruct(flatland.get());
    auto& hit_regions = uber_struct->local_hit_regions_map;

    EXPECT_EQ(hit_regions.size(), 1u);
    EXPECT_EQ(hit_regions[handle1].size(), 0u);
    EXPECT_EQ(hit_regions[handle2].size(), 1u);
  }
}

TEST_F(FlatlandTest, SetHitRegionsOverwritesPreviousOnes) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();
  const auto session_id = flatland->GetSessionId();

  const TransformId kId1 = {1};
  const TransformHandle handle1 = TransformHandle(session_id, 1);

  flatland->CreateTransform(kId1);
  flatland->SetRootTransform(kId1);

  PRESENT(flatland, true);

  constexpr float expected_initial_bounds = 1'000'000.F;
  // Check that the default hit region is as expected.
  {
    auto uber_struct = GetUberStruct(flatland.get());
    auto& hit_regions = uber_struct->local_hit_regions_map;

    EXPECT_EQ(hit_regions.size(), 1u);
    EXPECT_EQ(hit_regions[handle1].size(), 1u);

    auto hit_region = hit_regions[handle1][0];

    auto rect = hit_region.region;
    fuchsia::math::RectF expected_rect = {-expected_initial_bounds, -expected_initial_bounds,
                                          2 * expected_initial_bounds, 2 * expected_initial_bounds};
    ExpectRectFEquals(rect, expected_rect);
    EXPECT_EQ(hit_region.hit_test, fuchsia::ui::composition::HitTestInteraction::DEFAULT);
  }

  // Add a hit region to a different transform - this should not overwrite the default one.
  const TransformId kId2 = {2};
  const TransformHandle handle2 = TransformHandle(session_id, 2);

  flatland->CreateTransform(kId2);
  flatland->SetHitRegions(kId2,
                          {{{0, 1, 2, 3}, fuchsia::ui::composition::HitTestInteraction::DEFAULT}});

  PRESENT(flatland, true);

  {
    auto uber_struct = GetUberStruct(flatland.get());
    auto& hit_regions = uber_struct->local_hit_regions_map;

    EXPECT_EQ(hit_regions.size(), 2u);
    EXPECT_EQ(hit_regions[handle1].size(), 1u);
    EXPECT_EQ(hit_regions[handle2].size(), 1u);

    {
      auto hit_region = hit_regions[handle1][0];

      auto rect = hit_region.region;
      fuchsia::math::RectF expected_rect = {-expected_initial_bounds, -expected_initial_bounds,
                                            2 * expected_initial_bounds,
                                            2 * expected_initial_bounds};
      ExpectRectFEquals(rect, expected_rect);
      EXPECT_EQ(hit_region.hit_test, fuchsia::ui::composition::HitTestInteraction::DEFAULT);
    }

    {
      auto hit_region = hit_regions[handle2][0];

      auto rect = hit_region.region;
      fuchsia::math::RectF expected_rect = {0, 1, 2, 3};
      ExpectRectFEquals(rect, expected_rect);
      EXPECT_EQ(hit_region.hit_test, fuchsia::ui::composition::HitTestInteraction::DEFAULT);
    }
  }

  // Overwrite the default hit region.
  flatland->SetHitRegions(
      kId1, {{{1, 2, 3, 4}, fuchsia::ui::composition::HitTestInteraction::SEMANTICALLY_INVISIBLE}});

  PRESENT(flatland, true);

  {
    auto uber_struct = GetUberStruct(flatland.get());
    auto& hit_regions = uber_struct->local_hit_regions_map;

    EXPECT_EQ(hit_regions.size(), 2u);
    EXPECT_EQ(hit_regions[handle1].size(), 1u);

    auto hit_region = hit_regions[handle1][0];

    auto rect = hit_region.region;
    fuchsia::math::RectF expected_rect = {1, 2, 3, 4};
    ExpectRectFEquals(rect, expected_rect);
    EXPECT_EQ(hit_region.hit_test,
              fuchsia::ui::composition::HitTestInteraction::SEMANTICALLY_INVISIBLE);
  }
}

TEST_F(FlatlandTest, SetRootTransformAfterSetHitRegions_DoesNotChangeHitRegion) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();
  const auto session_id = flatland->GetSessionId();

  const TransformId kId1 = {1};
  const TransformHandle handle1 = TransformHandle(session_id, 1);

  flatland->CreateTransform(kId1);
  flatland->SetHitRegions(
      kId1, {{{0, 1, 2, 3}, fuchsia::ui::composition::HitTestInteraction::SEMANTICALLY_INVISIBLE}});
  flatland->SetRootTransform(kId1);

  PRESENT(flatland, true);

  auto uber_struct = GetUberStruct(flatland.get());
  auto& hit_regions = uber_struct->local_hit_regions_map;

  EXPECT_EQ(hit_regions.size(), 1u);
  EXPECT_EQ(hit_regions[handle1].size(), 1u);

  auto hit_region = hit_regions[handle1][0];

  auto rect = hit_region.region;
  fuchsia::math::RectF expected_rect = {0, 1, 2, 3};
  ExpectRectFEquals(rect, expected_rect);
  EXPECT_EQ(hit_region.hit_test,
            fuchsia::ui::composition::HitTestInteraction::SEMANTICALLY_INVISIBLE);
}

TEST_F(FlatlandTest, MultipleTransformsWithHitRegions) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();
  const auto session_id = flatland->GetSessionId();

  const TransformId kId1 = {1};
  const TransformHandle handle1 = TransformHandle(session_id, 1);

  flatland->CreateTransform(kId1);
  flatland->SetHitRegions(kId1,
                          {{{0, 1, 2, 3}, fuchsia::ui::composition::HitTestInteraction::DEFAULT}});

  const TransformId kId2 = {2};
  const TransformHandle handle2 = TransformHandle(session_id, 2);

  flatland->CreateTransform(kId2);
  flatland->SetHitRegions(
      kId2, {{{1, 2, 3, 4}, fuchsia::ui::composition::HitTestInteraction::SEMANTICALLY_INVISIBLE}});

  PRESENT(flatland, true);

  {
    auto uber_struct = GetUberStruct(flatland.get());
    auto& hit_regions = uber_struct->local_hit_regions_map;

    EXPECT_EQ(hit_regions.size(), 2u);
    EXPECT_EQ(hit_regions[handle1].size(), 1u);
    EXPECT_EQ(hit_regions[handle2].size(), 1u);

    auto hit_region1 = hit_regions[handle1][0];
    auto hit_region2 = hit_regions[handle2][0];

    auto rect1 = hit_region1.region;
    auto rect2 = hit_region2.region;
    fuchsia::math::RectF expected_rect1 = {0, 1, 2, 3};
    fuchsia::math::RectF expected_rect2 = {1, 2, 3, 4};
    ExpectRectFEquals(rect1, expected_rect1);
    ExpectRectFEquals(rect2, expected_rect2);

    EXPECT_EQ(hit_region1.hit_test, fuchsia::ui::composition::HitTestInteraction::DEFAULT);
    EXPECT_EQ(hit_region2.hit_test,
              fuchsia::ui::composition::HitTestInteraction::SEMANTICALLY_INVISIBLE);
  }
}

TEST_F(FlatlandTest, ManuallyAddedMaximalHitRegionPersists) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();
  const auto session_id = flatland->GetSessionId();

  const TransformId kId1 = {1};
  const TransformHandle handle1 = TransformHandle(session_id, 1);

  flatland->CreateTransform(kId1);
  flatland->SetHitRegions(kId1, {{{FLT_MIN, FLT_MIN, FLT_MAX, FLT_MAX},
                                  fuchsia::ui::composition::HitTestInteraction::DEFAULT}});
  PRESENT(flatland, true);

  flatland->SetRootTransform(kId1);
  PRESENT(flatland, true);

  flatland->SetRootTransform({0});
  PRESENT(flatland, true);

  auto uber_struct = GetUberStruct(flatland.get());
  auto& hit_regions = uber_struct->local_hit_regions_map;

  EXPECT_EQ(hit_regions.size(), 1u);
  EXPECT_EQ(hit_regions[handle1].size(), 1u);
  auto hit_region1 = hit_regions[handle1][0];

  auto rect = hit_region1.region;
  fuchsia::math::RectF expected_rect = {FLT_MIN, FLT_MIN, FLT_MAX, FLT_MAX};
  ExpectRectFEquals(rect, expected_rect);
}

TEST_F(FlatlandTest, ChildViewWatcherFailsIdCollision) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  const ContentId kId1 = {1};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  flatland->CreateViewport(kId1, std::move(parent_token), std::move(properties),
                           child_view_watcher.NewRequest());
  PRESENT(flatland, true);

  ViewportCreationToken parent_token2;
  ViewCreationToken child_token2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token2.value, &child_token2.value));

  flatland->CreateViewport(kId1, std::move(parent_token2), std::move(properties),
                           child_view_watcher.NewRequest());
  PRESENT(flatland, false);
}

TEST_F(FlatlandTest, ClearDelaysLinkDestructionUntilPresent) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  const ContentId kLinkId1 = {1};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  CreateViewport(parent.get(), child.get(), kLinkId1, &child_view_watcher,
                 &parent_viewport_watcher);

  EXPECT_TRUE(child_view_watcher.is_bound());
  EXPECT_TRUE(parent_viewport_watcher.is_bound());

  // Clearing the parent graph should not unbind the interfaces until Present() is called and the
  // acquire fence is signaled.
  parent->Clear();
  RunLoopUntilIdle();

  EXPECT_TRUE(child_view_watcher.is_bound());
  EXPECT_TRUE(parent_viewport_watcher.is_bound());

  PresentArgs args;
  args.acquire_fences = utils::CreateEventArray(1);
  auto event_copy = utils::CopyEvent(args.acquire_fences[0]);

  PRESENT_WITH_ARGS(parent, std::move(args), true);

  EXPECT_TRUE(child_view_watcher.is_bound());
  EXPECT_TRUE(parent_viewport_watcher.is_bound());

  // Signal the acquire fence to unbind the links.
  event_copy.signal(0, ZX_EVENT_SIGNALED);

  EXPECT_CALL(*mock_flatland_presenter_, ScheduleUpdateForSession(_, _, _, _));
  RunLoopUntilIdle();

  EXPECT_FALSE(child_view_watcher.is_bound());
  EXPECT_FALSE(parent_viewport_watcher.is_bound());

  // Recreate the Link. The parent graph was cleared so we can reuse the LinkId.
  CreateViewport(parent.get(), child.get(), kLinkId1, &child_view_watcher,
                 &parent_viewport_watcher);

  EXPECT_TRUE(child_view_watcher.is_bound());
  EXPECT_TRUE(parent_viewport_watcher.is_bound());

  // Clearing the child graph should not unbind the interfaces until Present() is called and the
  // acquire fence is signaled.
  child->Clear();
  RunLoopUntilIdle();

  EXPECT_TRUE(child_view_watcher.is_bound());
  EXPECT_TRUE(parent_viewport_watcher.is_bound());

  PresentArgs args2;
  args2.acquire_fences = utils::CreateEventArray(1);
  event_copy = utils::CopyEvent(args2.acquire_fences[0]);

  PRESENT_WITH_ARGS(child, std::move(args2), true);

  EXPECT_TRUE(child_view_watcher.is_bound());
  EXPECT_TRUE(parent_viewport_watcher.is_bound());

  // Signal the acquire fence to unbind the links.
  event_copy.signal(0, ZX_EVENT_SIGNALED);

  EXPECT_CALL(*mock_flatland_presenter_, ScheduleUpdateForSession(_, _, _, _));
  RunLoopUntilIdle();

  EXPECT_FALSE(child_view_watcher.is_bound());
  EXPECT_FALSE(parent_viewport_watcher.is_bound());
}

// This test doesn't use the helper function to create a link, because it tests intermediate steps
// and timing corner cases.
TEST_F(FlatlandTest, ChildGetsLayoutUpdateWithoutPresenting) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  // Set up a link, but don't call Present() on either instance.
  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  const ContentId kLinkId = {1};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size({1, 2});
  parent->CreateViewport(kLinkId, std::move(parent_token), std::move(properties),
                         child_view_watcher.NewRequest());

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  child->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(), NoViewProtocols(),
                     parent_viewport_watcher.NewRequest());

  // Request a layout update.
  std::optional<LayoutInfo> layout;
  parent_viewport_watcher->GetLayout([&](LayoutInfo info) { layout = std::move(info); });

  // Without even presenting, the child is able to get the initial properties from the parent->
  UpdateLinks(parent->GetRoot());
  EXPECT_TRUE(layout.has_value());
  EXPECT_EQ(1u, layout->logical_size().width);
  EXPECT_EQ(2u, layout->logical_size().height);
}

TEST_F(FlatlandTest, OverwrittenHangingGetsReturnError) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  // Set up a link, but don't call Present() on either instance.
  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  const ContentId kLinkId = {1};
  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size({1, 2});
  parent->CreateViewport(kLinkId, std::move(parent_token), std::move(properties),
                         child_view_watcher.NewRequest());

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  child->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(), NoViewProtocols(),
                     parent_viewport_watcher.NewRequest());
  UpdateLinks(parent->GetRoot());

  // First layout request should succeed immediately.
  bool layout_updated = false;
  parent_viewport_watcher->GetLayout([&](auto) { layout_updated = true; });
  RunLoopUntilIdle();
  EXPECT_TRUE(layout_updated);

  // Queue overwriting hanging gets.
  layout_updated = false;
  parent_viewport_watcher->GetLayout([&](auto) { layout_updated = true; });
  parent_viewport_watcher->GetLayout([&](auto) { layout_updated = true; });
  RunLoopUntilIdle();
  EXPECT_FALSE(layout_updated);

  // Present should fail on child because the client has broken flow control.
  PresentArgs args;
  args.expected_error = FlatlandError::BAD_HANGING_GET;
  PRESENT_WITH_ARGS(child, std::move(args), false);
}

// This test doesn't use the helper function to create a link, because it tests intermediate steps
// and timing corner cases.
TEST_F(FlatlandTest, ConnectedToDisplayParentPresentsBeforeChild) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  // Set up a link and attach it to the parent's root, but don't call Present() on either instance.
  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  const TransformId kTransformId = {1};

  parent->CreateTransform(kTransformId);
  parent->SetRootTransform(kTransformId);

  const ContentId kLinkId = {2};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size({1, 2});
  parent->CreateViewport(kLinkId, std::move(parent_token), std::move(properties),
                         child_view_watcher.NewRequest());
  parent->SetContent(kTransformId, kLinkId);

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  child->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(), NoViewProtocols(),
                     parent_viewport_watcher.NewRequest());

  // Request a status update.
  std::optional<ParentViewportStatus> parent_status;
  parent_viewport_watcher->GetStatus(
      [&](ParentViewportStatus status) { parent_status = std::move(status); });

  // The child begins disconnected from the display.
  UpdateLinks(parent->GetRoot());
  EXPECT_TRUE(parent_status.has_value());
  EXPECT_EQ(*parent_status, ParentViewportStatus::DISCONNECTED_FROM_DISPLAY);

  // The ParentViewportStatus will update when both the parent and child Present().
  parent_status.reset();
  parent_viewport_watcher->GetStatus(
      [&](ParentViewportStatus status) { parent_status = std::move(status); });

  // The parent presents first, no update.
  PRESENT(parent, true);
  UpdateLinks(parent->GetRoot());
  EXPECT_FALSE(parent_status.has_value());

  // The child presents second and the status updates.
  PRESENT(child, true);
  UpdateLinks(parent->GetRoot());
  EXPECT_TRUE(parent_status.has_value());
  EXPECT_EQ(*parent_status, ParentViewportStatus::CONNECTED_TO_DISPLAY);
}

// This test doesn't use the helper function to create a link, because it tests intermediate steps
// and timing corner cases.
TEST_F(FlatlandTest, ConnectedToDisplayChildPresentsBeforeParent) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  // Set up a link and attach it to the parent's root, but don't call Present() on either instance.
  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  const TransformId kTransformId = {1};

  parent->CreateTransform(kTransformId);
  parent->SetRootTransform(kTransformId);

  const ContentId kLinkId = {2};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size({1, 2});
  parent->CreateViewport(kLinkId, std::move(parent_token), std::move(properties),
                         child_view_watcher.NewRequest());
  parent->SetContent(kTransformId, kLinkId);

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  child->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(), NoViewProtocols(),
                     parent_viewport_watcher.NewRequest());

  // Request a status update.
  std::optional<ParentViewportStatus> parent_status;
  parent_viewport_watcher->GetStatus(
      [&](ParentViewportStatus status) { parent_status = std::move(status); });

  // The child begins disconnected from the display.
  UpdateLinks(parent->GetRoot());
  EXPECT_TRUE(parent_status.has_value());
  EXPECT_EQ(*parent_status, ParentViewportStatus::DISCONNECTED_FROM_DISPLAY);

  // The ParentViewportStatus will update when both the parent and child Present().
  parent_status.reset();
  parent_viewport_watcher->GetStatus(
      [&](ParentViewportStatus status) { parent_status = std::move(status); });

  // The child presents first, no update.
  PRESENT(child, true);
  UpdateLinks(parent->GetRoot());
  EXPECT_FALSE(parent_status.has_value());

  // The parent presents second and the status updates.
  PRESENT(parent, true);
  UpdateLinks(parent->GetRoot());
  EXPECT_TRUE(parent_status.has_value());
  EXPECT_EQ(*parent_status, ParentViewportStatus::CONNECTED_TO_DISPLAY);
}

// This test doesn't use the helper function to create a link, because it tests intermediate steps
// and timing corner cases.
TEST_F(FlatlandTest, ChildReceivesDisconnectedFromDisplay) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  // Set up a link and attach it to the parent's root, but don't call Present() on either instance.
  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  const TransformId kTransformId = {1};

  parent->CreateTransform(kTransformId);
  parent->SetRootTransform(kTransformId);

  const ContentId kLinkId = {2};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size({1, 2});
  parent->CreateViewport(kLinkId, std::move(parent_token), std::move(properties),
                         child_view_watcher.NewRequest());
  parent->SetContent(kTransformId, kLinkId);

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  child->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(), NoViewProtocols(),
                     parent_viewport_watcher.NewRequest());

  // The ParentViewportStatus will update when both the parent and child Present().
  std::optional<ParentViewportStatus> parent_status;
  parent_viewport_watcher->GetStatus(
      [&](ParentViewportStatus status) { parent_status = std::move(status); });

  PRESENT(child, true);
  PRESENT(parent, true);
  UpdateLinks(parent->GetRoot());
  EXPECT_TRUE(parent_status.has_value());
  EXPECT_EQ(*parent_status, ParentViewportStatus::CONNECTED_TO_DISPLAY);

  // The ParentViewportStatus will update again if the parent removes the child link from its
  // topology.
  parent_status.reset();
  parent_viewport_watcher->GetStatus(
      [&](ParentViewportStatus status) { parent_status = std::move(status); });

  parent->SetContent(kTransformId, {0});
  PRESENT(parent, true);

  UpdateLinks(parent->GetRoot());
  EXPECT_TRUE(parent_status.has_value());
  EXPECT_EQ(*parent_status, ParentViewportStatus::DISCONNECTED_FROM_DISPLAY);
}

// This test doesn't use the helper function to create a link, because it tests intermediate steps
// and timing corner cases.
TEST_F(FlatlandTest, ValidChildToParentFlow_ChildUsedCreateView2) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  ViewportCreationToken parent_viewport_token;
  ViewCreationToken child_view_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_viewport_token.value, &child_view_token.value));

  const ContentId kLinkId = {1};
  const TransformId kRootTransform = {1};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size({1, 2});
  parent->CreateViewport(kLinkId, std::move(parent_viewport_token), std::move(properties),
                         child_view_watcher.NewRequest());

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  child->CreateView2(std::move(child_view_token), scenic::NewViewIdentityOnCreation(),
                     NoViewProtocols(), parent_viewport_watcher.NewRequest());

  std::optional<ChildViewStatus> child_status;
  child_view_watcher->GetStatus([&](ChildViewStatus status) {
    ASSERT_EQ(ChildViewStatus::CONTENT_HAS_PRESENTED, status);
    child_status = std::move(status);
  });

  std::optional<fuchsia::ui::views::ViewRef> child_viewref;
  child_view_watcher->GetViewRef(
      [&](fuchsia::ui::views::ViewRef viewref) { child_viewref = std::move(viewref); });

  // ChildViewStatus changes as soon as the child presents. The parent does not have to present.
  EXPECT_FALSE(child_status.has_value());

  PRESENT(child, true);
  UpdateLinks(parent->GetRoot());
  EXPECT_TRUE(child_status.has_value());

  // Note that although CONTENT_HAS_PRESENTED is signaled, GetViewRef() does not yet return the ref.
  // This is because although the parent and child are connected, neither appears in the global
  // topology, because neither is connected to the root.
  EXPECT_FALSE(child_viewref.has_value());

  // Having the parent present is still not sufficient to fulfill GetViewRef(), because the viewport
  // has not been added to the parent's local sub-tree, and therefore doesn't appear in the global
  // topology.
  PRESENT(parent, true);
  UpdateLinks(parent->GetRoot());
  EXPECT_FALSE(child_viewref.has_value());

  // Adding the viewport to the parent's local tree, then presenting, is sufficient for the call to
  // GetViewRef() to succeed.
  parent->CreateTransform(kRootTransform);
  parent->SetRootTransform(kRootTransform);
  parent->SetContent(kRootTransform, kLinkId);

  // While we're at it, add an acquire fence and make sure that the ViewRef isn't sent until the
  // event is signaled.
  PresentArgs args;
  args.acquire_fences = utils::CreateEventArray(1);
  auto event_copy = utils::CopyEvent(args.acquire_fences[0]);

  // We still don't get the ViewRef, because we haven't signaled the event.
  PRESENT_WITH_ARGS(parent, std::move(args), true);
  UpdateLinks(parent->GetRoot());
  EXPECT_FALSE(child_viewref.has_value());

  // Signal the acquire fence to unblock the present.
  event_copy.signal(0, ZX_EVENT_SIGNALED);
  EXPECT_CALL(*mock_flatland_presenter_, ScheduleUpdateForSession(_, _, _, _));
  RunLoopUntilIdle();
  ApplySessionUpdatesAndSignalFences();
  UpdateLinks(parent->GetRoot());
  EXPECT_TRUE(child_viewref.has_value());
  EXPECT_TRUE(child_viewref.value().reference);
}

TEST_F(FlatlandTest, ValidChildToParentFlow_ChildUsedCreateView) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  ViewportCreationToken parent_viewport_token;
  ViewCreationToken child_view_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_viewport_token.value, &child_view_token.value));

  const ContentId kLinkId = {1};
  const TransformId kRootTransform = {1};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size({1, 2});
  parent->CreateViewport(kLinkId, std::move(parent_viewport_token), std::move(properties),
                         child_view_watcher.NewRequest());

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  child->CreateView(std::move(child_view_token), parent_viewport_watcher.NewRequest());

  std::optional<ChildViewStatus> child_status;
  child_view_watcher->GetStatus([&](ChildViewStatus status) { child_status = std::move(status); });

  // ChildViewStatus changes as soon as the child presents. The parent does not have to present.
  EXPECT_FALSE(child_status.has_value());

  PRESENT(child, true);
  UpdateLinks(parent->GetRoot());
  EXPECT_TRUE(child_status.has_value());
  ASSERT_EQ(ChildViewStatus::CONTENT_HAS_PRESENTED, *child_status);
}

TEST_F(FlatlandTest, ContentHasPresentedSignalWaitsForAcquireFences) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  const ContentId kLinkId = {1};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size({1, 2});
  parent->CreateViewport(kLinkId, std::move(parent_token), std::move(properties),
                         child_view_watcher.NewRequest());

  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  child->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(), NoViewProtocols(),
                     parent_viewport_watcher.NewRequest());

  std::optional<ChildViewStatus> cvs;
  child_view_watcher->GetStatus([&cvs](ChildViewStatus status) {
    ASSERT_EQ(ChildViewStatus::CONTENT_HAS_PRESENTED, status);
    cvs = status;
  });

  // ChildViewStatus changes as soon as the child presents. The parent does not have to present.
  EXPECT_FALSE(cvs.has_value());

  // Present the child with unsignalled acquire fence.
  PresentArgs args;
  args.acquire_fences = utils::CreateEventArray(1);
  auto acquire1_copy = utils::CopyEvent(args.acquire_fences[0]);
  EXPECT_FALSE(utils::IsEventSignalled(args.acquire_fences[0], ZX_EVENT_SIGNALED));
  PRESENT_WITH_ARGS(child, std::move(args), true);

  // Hanging get should not be run yet because the fence is not signalled.
  UpdateLinks(parent->GetRoot());
  EXPECT_FALSE(cvs.has_value());

  // Signal the acquire fence.
  EXPECT_CALL(*mock_flatland_presenter_, ScheduleUpdateForSession(_, _, _, _));
  acquire1_copy.signal(0, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  ApplySessionUpdatesAndSignalFences();

  // Hanging get should run now.
  UpdateLinks(parent->GetRoot());
  EXPECT_TRUE(cvs.has_value());
  EXPECT_EQ(cvs.value(), ChildViewStatus::CONTENT_HAS_PRESENTED);
}

TEST_F(FlatlandTest, LayoutOnlyUpdatesChildrenInGlobalTopology) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  const TransformId kTransformId = {1};
  const ContentId kLinkId = {2};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  CreateViewport(parent.get(), child.get(), kLinkId, &child_view_watcher, &parent_viewport_watcher);
  UpdateLinks(parent->GetRoot());

  // Confirm that the initial logical size is available immediately.
  {
    std::optional<LayoutInfo> layout;
    parent_viewport_watcher->GetLayout([&](LayoutInfo info) { layout = std::move(info); });

    EXPECT_FALSE(layout.has_value());
    UpdateLinks(parent->GetRoot());
    EXPECT_TRUE(layout.has_value());
    EXPECT_EQ(kDefaultSize, layout->logical_size().width);
    EXPECT_EQ(kDefaultSize, layout->logical_size().height);
  }

  // Set the logical size to something new.
  {
    ViewportProperties properties;
    properties.set_logical_size({2, 3});
    parent->SetViewportProperties(kLinkId, std::move(properties));
    PRESENT(parent, true);
  }

  {
    std::optional<LayoutInfo> layout;
    parent_viewport_watcher->GetLayout([&](LayoutInfo info) { layout = std::move(info); });

    // Confirm that no update is triggered since the child is not in the global topology.
    EXPECT_FALSE(layout.has_value());
    UpdateLinks(parent->GetRoot());
    EXPECT_FALSE(layout.has_value());

    // Attach the child to the global topology.
    parent->CreateTransform(kTransformId);
    parent->SetRootTransform(kTransformId);
    parent->SetContent(kTransformId, kLinkId);
    PRESENT(parent, true);

    // Confirm that the new logical size is accessible.
    EXPECT_FALSE(layout.has_value());
    UpdateLinks(parent->GetRoot());
    EXPECT_TRUE(layout.has_value());
    EXPECT_EQ(2u, layout->logical_size().width);
    EXPECT_EQ(3u, layout->logical_size().height);
  }
}

TEST_F(FlatlandTest, SetViewportProperties_WithDeadViewport_ShouldNotCrash) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  const TransformId kTransformId = {1};
  const ContentId kLinkId = {2};

  ViewportCreationToken parent_token;
  {
    ViewCreationToken child_token;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));
    // Child token goes out of scope.
  }

  {
    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
    ViewportProperties properties;
    properties.set_logical_size({kDefaultSize, kDefaultSize});
    parent->CreateViewport(kLinkId, std::move(parent_token), std::move(properties),
                           child_view_watcher.NewRequest());
    PRESENT(parent, true);
  }

  {
    // Now call SetViewportProperties() and make sure we don't crash.
    parent->SetViewportProperties(kLinkId, ViewportProperties{});
    PRESENT(parent, true);
  }
}

TEST_F(FlatlandTest, SetViewportPropertiesDefaultBehavior) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  const TransformId kTransformId = {1};
  const ContentId kLinkId = {2};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  CreateViewport(parent.get(), child.get(), kLinkId, &child_view_watcher, &parent_viewport_watcher);

  parent->CreateTransform(kTransformId);
  parent->SetRootTransform(kTransformId);
  parent->SetContent(kTransformId, kLinkId);
  PRESENT(parent, true);

  UpdateLinks(parent->GetRoot());

  // Confirm that the initial layout is the default.
  {
    std::optional<LayoutInfo> layout;
    parent_viewport_watcher->GetLayout([&](LayoutInfo info) { layout = std::move(info); });

    EXPECT_FALSE(layout.has_value());
    UpdateLinks(parent->GetRoot());
    EXPECT_TRUE(layout.has_value());
    EXPECT_EQ(kDefaultSize, layout->logical_size().width);
    EXPECT_EQ(kDefaultSize, layout->logical_size().height);
    EXPECT_EQ(kDefaultInset, layout->inset().top);
    EXPECT_EQ(kDefaultInset, layout->inset().left);
    EXPECT_EQ(kDefaultInset, layout->inset().bottom);
    EXPECT_EQ(kDefaultInset, layout->inset().right);
  }

  // Set the logical size to something new.
  {
    ViewportProperties properties;
    properties.set_logical_size({2, 3});
    parent->SetViewportProperties(kLinkId, std::move(properties));
    PRESENT(parent, true);
  }

  // Confirm that the new logical size is accessible.
  {
    std::optional<LayoutInfo> layout;
    parent_viewport_watcher->GetLayout([&](LayoutInfo info) { layout = std::move(info); });

    EXPECT_FALSE(layout.has_value());
    UpdateLinks(parent->GetRoot());
    EXPECT_TRUE(layout.has_value());
    EXPECT_EQ(2u, layout->logical_size().width);
    EXPECT_EQ(3u, layout->logical_size().height);
    EXPECT_EQ(kDefaultInset, layout->inset().top);
    EXPECT_EQ(kDefaultInset, layout->inset().left);
    EXPECT_EQ(kDefaultInset, layout->inset().bottom);
    EXPECT_EQ(kDefaultInset, layout->inset().right);
  }

  // Set the inset to something new.
  {
    ViewportProperties properties;
    properties.set_inset({4, 5, 6, 7});
    parent->SetViewportProperties(kLinkId, std::move(properties));
    PRESENT(parent, true);
  }

  // Confirm that the new logical size is updated.
  {
    std::optional<LayoutInfo> layout;
    parent_viewport_watcher->GetLayout([&](LayoutInfo info) { layout = std::move(info); });

    EXPECT_FALSE(layout.has_value());
    UpdateLinks(parent->GetRoot());
    EXPECT_TRUE(layout.has_value());
    EXPECT_EQ(2u, layout->logical_size().width);
    EXPECT_EQ(3u, layout->logical_size().height);
    EXPECT_EQ(4, layout->inset().top);
    EXPECT_EQ(5, layout->inset().right);
    EXPECT_EQ(6, layout->inset().bottom);
    EXPECT_EQ(7, layout->inset().left);
  }

  // Set link properties using a properties object with an unset size field.
  {
    ViewportProperties default_properties;
    parent->SetViewportProperties(kLinkId, std::move(default_properties));
    PRESENT(parent, true);
  }

  // Confirm that no update has been triggered.
  {
    std::optional<LayoutInfo> layout;
    parent_viewport_watcher->GetLayout([&](LayoutInfo info) { layout = std::move(info); });

    EXPECT_FALSE(layout.has_value());
    UpdateLinks(parent->GetRoot());
    EXPECT_FALSE(layout.has_value());
  }
}

// Test to make sure that if we have a transform containing a viewport as content,
// that itself has two or more parents, that the viewport properties that are returned
// to the parent_viewport_watcher are those calculated from the subtree containing the
// smallest overall scale factor.
TEST_F(FlatlandTest, SetViewportPropertiesMultiParenting) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  const TransformId kTransformId = {1};
  const TransformId kNormalParentId = {2};
  const TransformId kMagParentId = {3};
  const TransformId kChildId = {4};
  const ContentId kLinkId = {5};
  const SizeU kMagScale = {5, 5};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  CreateViewport(parent.get(), child.get(), kLinkId, &child_view_watcher, &parent_viewport_watcher);

  // Create the diamond structure used for magnification in the
  // parent instance, with the magnification transform having a
  // non-unit scale factor.
  parent->CreateTransform(kTransformId);
  parent->CreateTransform(kNormalParentId);
  parent->CreateTransform(kMagParentId);
  parent->CreateTransform(kChildId);

  parent->SetScale(kNormalParentId, {1, 1});
  parent->SetScale(kMagParentId, {5, 5});

  parent->SetRootTransform(kTransformId);
  parent->AddChild(kTransformId, kNormalParentId);
  parent->AddChild(kTransformId, kMagParentId);
  parent->AddChild(kNormalParentId, kChildId);
  parent->AddChild(kMagParentId, kChildId);

  parent->SetContent(kChildId, kLinkId);
  PRESENT(parent, true);
}

TEST_F(FlatlandTest, SetViewportPropertiesMultisetBehavior) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  const TransformId kTransformId = {1};
  const ContentId kLinkId = {2};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  CreateViewport(parent.get(), child.get(), kLinkId, &child_view_watcher, &parent_viewport_watcher);

  // Our initial layout (from link creation) should be the default size.
  {
    int num_updates = 0;
    parent_viewport_watcher->GetLayout([&](LayoutInfo info) {
      EXPECT_EQ(kDefaultSize, info.logical_size().width);
      EXPECT_EQ(kDefaultSize, info.logical_size().height);
      ++num_updates;
    });

    EXPECT_EQ(0, num_updates);
    UpdateLinks(parent->GetRoot());
    EXPECT_EQ(1, num_updates);
  }

  // Create a full chain of transforms from parent root to child root.
  parent->CreateTransform(kTransformId);
  parent->SetRootTransform(kTransformId);
  parent->SetContent(kTransformId, kLinkId);
  PRESENT(parent, true);

  const uint32_t kInitialSize = 100;

  // Set the logical size to something new multiple times.
  for (int i = 10; i >= 0; --i) {
    ViewportProperties properties;
    properties.set_logical_size({kInitialSize + i + 1, kInitialSize + i + 1});
    parent->SetViewportProperties(kLinkId, std::move(properties));
    ViewportProperties properties2;
    properties2.set_logical_size({kInitialSize + i, kInitialSize + i});
    parent->SetViewportProperties(kLinkId, std::move(properties2));
    PRESENT(parent, true);
  }

  // Confirm that the callback is fired once, and that it has the most up-to-date data.
  {
    int num_updates = 0;
    parent_viewport_watcher->GetLayout([&](LayoutInfo info) {
      EXPECT_EQ(kInitialSize, info.logical_size().width);
      EXPECT_EQ(kInitialSize, info.logical_size().height);
      ++num_updates;
    });

    EXPECT_EQ(0, num_updates);
    UpdateLinks(parent->GetRoot());
    EXPECT_EQ(1, num_updates);
  }

  const uint32_t kNewSize = 50u;

  // Confirm that calling GetLayout again results in a hung get.
  int num_updates = 0;
  parent_viewport_watcher->GetLayout([&](LayoutInfo info) {
    // When we receive the new layout information, confirm that we receive the last update in the
    // batch.
    EXPECT_EQ(kNewSize, info.logical_size().width);
    EXPECT_EQ(kNewSize, info.logical_size().height);
    ++num_updates;
  });

  EXPECT_EQ(0, num_updates);
  UpdateLinks(parent->GetRoot());
  EXPECT_EQ(0, num_updates);

  // Update the properties twice, once with the old value, once with the new value.
  {
    ViewportProperties properties;
    properties.set_logical_size({kInitialSize, kInitialSize});
    parent->SetViewportProperties(kLinkId, std::move(properties));
    ViewportProperties properties2;
    properties2.set_logical_size({kNewSize, kNewSize});
    parent->SetViewportProperties(kLinkId, std::move(properties2));
    PRESENT(parent, true);
  }

  // Confirm that we receive the update.
  EXPECT_EQ(0, num_updates);
  UpdateLinks(parent->GetRoot());
  EXPECT_EQ(1, num_updates);
}

TEST_F(FlatlandTest, SetViewportPropertiesOnMultipleChildren) {
  const int kNumChildren = 3;
  const TransformId kRootTransform = {1};
  const TransformId kTransformIds[kNumChildren] = {{2}, {3}, {4}};
  const ContentId kLinkIds[kNumChildren] = {{5}, {6}, {7}};

  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> children[kNumChildren] = {CreateFlatland(), CreateFlatland(),
                                                      CreateFlatland()};
  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher[kNumChildren];
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher[kNumChildren];

  parent->CreateTransform(kRootTransform);
  parent->SetRootTransform(kRootTransform);

  for (int i = 0; i < kNumChildren; ++i) {
    parent->CreateTransform(kTransformIds[i]);
    parent->AddChild(kRootTransform, kTransformIds[i]);
    CreateViewport(parent.get(), children[i].get(), kLinkIds[i], &child_view_watcher[i],
                   &parent_viewport_watcher[i]);
    parent->SetContent(kTransformIds[i], kLinkIds[i]);
  }
  UpdateLinks(parent->GetRoot());

  // Confirm that all children are at the default value
  for (int i = 0; i < kNumChildren; ++i) {
    std::optional<LayoutInfo> layout;
    parent_viewport_watcher[i]->GetLayout([&](LayoutInfo info) { layout = std::move(info); });

    EXPECT_FALSE(layout.has_value());
    UpdateLinks(parent->GetRoot());
    EXPECT_TRUE(layout.has_value());
    EXPECT_EQ(kDefaultSize, layout->logical_size().width);
    EXPECT_EQ(kDefaultSize, layout->logical_size().height);
  }

  // Resize the content on all children.
  for (auto id : kLinkIds) {
    SizeU size;
    size.width = id.value;
    size.height = id.value * 2;
    ViewportProperties properties;
    properties.set_logical_size(size);
    parent->SetViewportProperties(id, std::move(properties));
  }

  PRESENT(parent, true);

  for (int i = 0; i < kNumChildren; ++i) {
    std::optional<LayoutInfo> layout;
    parent_viewport_watcher[i]->GetLayout([&](LayoutInfo info) { layout = std::move(info); });

    EXPECT_FALSE(layout.has_value());
    UpdateLinks(parent->GetRoot());
    EXPECT_TRUE(layout.has_value());
    EXPECT_EQ(kLinkIds[i].value, layout->logical_size().width);
    EXPECT_EQ(kLinkIds[i].value * 2, layout->logical_size().height);
  }
}

// Make sure that if we set the initial dpr of the link system, that
// it gets transmitted to the layout info.
TEST_F(FlatlandTest, LinkSystemInitialDevicePixelRatioTest) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  const TransformId kTransformId = {1};
  const ContentId kLinkId = {2};

  glm::vec2 initial_dpr = {3.f, 4.f};
  link_system_->set_initial_device_pixel_ratio(initial_dpr);

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  CreateViewport(parent.get(), child.get(), kLinkId, &child_view_watcher, &parent_viewport_watcher);

  int num_updates = 0;
  parent_viewport_watcher->GetLayout([&](LayoutInfo info) {
    EXPECT_EQ(kDefaultSize, info.logical_size().width);
    EXPECT_EQ(kDefaultSize, info.logical_size().height);
    EXPECT_EQ(initial_dpr.x, info.device_pixel_ratio().x);
    EXPECT_EQ(initial_dpr.y, info.device_pixel_ratio().x);
    // Reset the link system
    link_system_->set_initial_device_pixel_ratio(glm::vec2(1.f, 1.f));
  });
}

TEST_F(FlatlandTest, SetLinkOnTransformErrorCases) {
  const TransformId kId1 = {1};
  const TransformId kId2 = {2};

  const ContentId kLinkId1 = {1};
  const ContentId kLinkId2 = {2};

  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    // Creating a link with an empty property object is an error. Logical size must be provided at
    // creation time.
    ViewportCreationToken parent_token;
    ViewCreationToken child_token;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));
    ViewportProperties empty_properties;
    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
    flatland->CreateViewport(kLinkId1, std::move(parent_token), std::move(empty_properties),
                             child_view_watcher.NewRequest());

    PRESENT(flatland, false);
  }

  // Setup.
  auto SetupFlatland = [&]() {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    ViewportCreationToken parent_token;
    ViewCreationToken child_token;
    zx::channel::create(0, &parent_token.value, &child_token.value);
    ViewportProperties properties;
    properties.set_logical_size({kDefaultSize, kDefaultSize});
    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
    flatland->CreateViewport(kLinkId1, std::move(parent_token), std::move(properties),
                             child_view_watcher.NewRequest());
    return flatland;
  };

  // Zero is not a valid transform_id.
  {
    auto flatland = SetupFlatland();
    PRESENT(flatland, true);

    flatland->SetContent({0}, kLinkId1);
    PRESENT(flatland, false);
  }

  // Setting a valid link on an invalid transform is not valid.
  {
    auto flatland = SetupFlatland();
    PRESENT(flatland, true);

    flatland->SetContent(kId2, kLinkId1);
    PRESENT(flatland, false);
  }

  // Setting an invalid link on a valid transform is not valid.
  {
    auto flatland = SetupFlatland();
    flatland->CreateTransform(kId1);
    PRESENT(flatland, true);

    flatland->SetContent(kId1, kLinkId2);
    PRESENT(flatland, false);
  }
}

TEST_F(FlatlandTest, ReleaseViewportErrorCases) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();

  // Zero is not a valid link_id.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->ReleaseViewport({0}, [](ViewportCreationToken token) { EXPECT_TRUE(false); });
    PRESENT(flatland, false);
  }

  // Using a link_id that does not exist is not valid.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    const ContentId kLinkId1 = {1};
    flatland->ReleaseViewport(kLinkId1, [](ViewportCreationToken token) { EXPECT_TRUE(false); });
    PRESENT(flatland, false);
  }

  // ContentId is not a Link.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    const ContentId kImageId = {2};
    BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();

    ImageProperties properties;
    properties.set_size({100, 200});

    CreateImage(flatland.get(), allocator.get(), kImageId, std::move(ref_pair),
                std::move(properties));

    flatland->ReleaseViewport(kImageId, [](ViewportCreationToken token) { EXPECT_TRUE(false); });
    PRESENT(flatland, false);
  }
}

TEST_F(FlatlandTest, ReleaseViewportReturnsOriginalToken) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  const zx_koid_t expected_koid = fsl::GetKoid(parent_token.value.get());

  const ContentId kLinkId1 = {1};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  flatland->CreateViewport(kLinkId1, std::move(parent_token), std::move(properties),
                           child_view_watcher.NewRequest());
  PRESENT(flatland, true);

  ViewportCreationToken content_token;
  flatland->ReleaseViewport(kLinkId1, [&content_token](ViewportCreationToken token) {
    content_token = std::move(token);
  });

  RunLoopUntilIdle();

  // Until Present() is called and the acquire fence is signaled, the previous ChildViewWatcher is
  // not unbound.
  EXPECT_TRUE(child_view_watcher.is_bound());
  EXPECT_FALSE(content_token.value.is_valid());

  PresentArgs args;
  args.acquire_fences = utils::CreateEventArray(1);
  auto event_copy = utils::CopyEvent(args.acquire_fences[0]);

  PRESENT_WITH_ARGS(flatland, std::move(args), true);

  EXPECT_TRUE(child_view_watcher.is_bound());
  EXPECT_FALSE(content_token.value.is_valid());

  // Signal the acquire fence to unbind the link.
  event_copy.signal(0, ZX_EVENT_SIGNALED);

  EXPECT_CALL(*mock_flatland_presenter_, ScheduleUpdateForSession(_, _, _, _));
  RunLoopUntilIdle();

  EXPECT_FALSE(child_view_watcher.is_bound());
  EXPECT_TRUE(content_token.value.is_valid());
  EXPECT_EQ(fsl::GetKoid(content_token.value.get()), expected_koid);
}

TEST_F(FlatlandTest, ReleaseViewportReturnsOrphanedTokenOnChildDeath) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  const ContentId kLinkId1 = {1};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  flatland->CreateViewport(kLinkId1, std::move(parent_token), std::move(properties),
                           child_view_watcher.NewRequest());
  PRESENT(flatland, true);

  // Killing the peer token does not prevent the instance from returning a valid token.
  child_token.value.reset();
  RunLoopUntilIdle();

  ViewportCreationToken content_token;
  flatland->ReleaseViewport(kLinkId1, [&content_token](ViewportCreationToken token) {
    content_token = std::move(token);
  });
  PRESENT(flatland, true);

  EXPECT_TRUE(content_token.value.is_valid());

  // But trying to link with that token will immediately fail because it is already orphaned.
  const ContentId kLinkId2 = {2};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher2;
  flatland->CreateViewport(kLinkId2, std::move(content_token), std::move(properties),
                           child_view_watcher2.NewRequest());
  PRESENT(flatland, true);

  EXPECT_FALSE(child_view_watcher2.is_bound());
}

TEST_F(FlatlandTest, CreateViewportPresentedBeforeCreateView) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  // Create a transform, add it to the parent, then create a link and assign to the transform.
  const TransformId kId1 = {1};
  parent->CreateTransform(kId1);
  parent->SetRootTransform(kId1);

  const ContentId kLinkId = {1};

  fidl::InterfacePtr<ChildViewWatcher> parent_child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  parent->CreateViewport(kLinkId, std::move(parent_token), std::move(properties),
                         parent_child_view_watcher.NewRequest());
  parent->SetContent(kId1, kLinkId);

  PRESENT(parent, true);

  // Link the child to the parent->
  fidl::InterfacePtr<ParentViewportWatcher> child_parent_viewport_watcher;
  child->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(), NoViewProtocols(),
                     child_parent_viewport_watcher.NewRequest());

  // The child should only be accessible from the parent when Present() is called on the child->
  EXPECT_FALSE(IsDescendantOf(parent->GetRoot(), child->GetRoot()));

  PRESENT(child, true);

  EXPECT_TRUE(IsDescendantOf(parent->GetRoot(), child->GetRoot()));
}

TEST_F(FlatlandTest, CreateViewPresentedBeforeCreateViewport) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  // Link the child to the parent
  fidl::InterfacePtr<ParentViewportWatcher> child_parent_viewport_watcher;
  child->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(), NoViewProtocols(),
                     child_parent_viewport_watcher.NewRequest());

  PRESENT(child, true);

  // Create a transform, add it to the parent, then create a link and assign to the transform.
  const TransformId kId1 = {1};
  parent->CreateTransform(kId1);
  parent->SetRootTransform(kId1);

  // Present the parent once so that it has a topology or else IsDescendantOf() will crash.
  PRESENT(parent, true);

  const ContentId kLinkId = {1};

  fidl::InterfacePtr<ChildViewWatcher> parent_child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  parent->CreateViewport(kLinkId, std::move(parent_token), std::move(properties),
                         parent_child_view_watcher.NewRequest());
  parent->SetContent(kId1, kLinkId);

  // The child should only be accessible from the parent when Present() is called on the parent->
  EXPECT_FALSE(IsDescendantOf(parent->GetRoot(), child->GetRoot()));

  PRESENT(parent, true);

  EXPECT_TRUE(IsDescendantOf(parent->GetRoot(), child->GetRoot()));
}

TEST_F(FlatlandTest, LinkResolvedBeforeEitherPresent) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  // Create a transform, add it to the parent, then create a link and assign to the transform.
  const TransformId kId1 = {1};
  parent->CreateTransform(kId1);
  parent->SetRootTransform(kId1);

  // Present the parent once so that it has a topology or else IsDescendantOf() will crash.
  PRESENT(parent, true);

  const ContentId kLinkId = {1};

  fidl::InterfacePtr<ChildViewWatcher> parent_child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  parent->CreateViewport(kLinkId, std::move(parent_token), std::move(properties),
                         parent_child_view_watcher.NewRequest());
  parent->SetContent(kId1, kLinkId);

  // Link the child to the parent->
  fidl::InterfacePtr<ParentViewportWatcher> child_parent_viewport_watcher;
  child->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(), NoViewProtocols(),
                     child_parent_viewport_watcher.NewRequest());

  // The child should only be accessible from the parent when Present() is called on both the parent
  // and the child->
  EXPECT_FALSE(IsDescendantOf(parent->GetRoot(), child->GetRoot()));

  PRESENT(parent, true);

  EXPECT_FALSE(IsDescendantOf(parent->GetRoot(), child->GetRoot()));

  PRESENT(child, true);

  EXPECT_TRUE(IsDescendantOf(parent->GetRoot(), child->GetRoot()));
}

TEST_F(FlatlandTest, ClearLinkToChild) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  ViewportCreationToken parent_token;
  ViewCreationToken child_token;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

  // Create and link the two instances.
  const TransformId kId1 = {1};
  parent->CreateTransform(kId1);
  parent->SetRootTransform(kId1);

  const ContentId kLinkId = {1};

  fidl::InterfacePtr<ChildViewWatcher> parent_child_view_watcher;
  ViewportProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  parent->CreateViewport(kLinkId, std::move(parent_token), std::move(properties),
                         parent_child_view_watcher.NewRequest());
  parent->SetContent(kId1, kLinkId);

  fidl::InterfacePtr<ParentViewportWatcher> child_parent_viewport_watcher;
  child->CreateView2(std::move(child_token), scenic::NewViewIdentityOnCreation(), NoViewProtocols(),
                     child_parent_viewport_watcher.NewRequest());

  PRESENT(parent, true);
  PRESENT(child, true);

  EXPECT_TRUE(IsDescendantOf(parent->GetRoot(), child->GetRoot()));

  // Reset the child link using zero as the link id.
  parent->SetContent(kId1, {0});

  PRESENT(parent, true);

  EXPECT_FALSE(IsDescendantOf(parent->GetRoot(), child->GetRoot()));
}

// TODO(fxb/81576): Reenable.
TEST_F(FlatlandTest, DISABLED_RelinkUnlinkedParentSameToken) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  // Create link and Present.
  const ContentId kLinkId1 = {1};
  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  CreateViewport(parent.get(), child.get(), kLinkId1, &child_view_watcher,
                 &parent_viewport_watcher);
  RunLoopUntilIdle();

  const TransformId kId1 = {1};
  parent->CreateTransform(kId1);
  parent->SetRootTransform(kId1);
  parent->SetContent(kId1, kLinkId1);
  PRESENT(parent, true);
  EXPECT_TRUE(IsDescendantOf(parent->GetRoot(), child->GetRoot()));

  // Both protocols should be bound at this point.
  EXPECT_TRUE(child_view_watcher.is_bound());

  EXPECT_TRUE(parent_viewport_watcher.is_bound());
  bool parent_viewport_watcher_updated = false;
  parent_viewport_watcher->GetLayout(
      [&parent_viewport_watcher_updated](auto) { parent_viewport_watcher_updated = true; });
  EXPECT_FALSE(parent_viewport_watcher_updated);
  RunLoopUntilIdle();
  EXPECT_TRUE(parent_viewport_watcher_updated);

  // Unlink the parent on child.
  ViewCreationToken view_creation_token;
  // child->ReleaseView([&view_creation_token](ViewCreationToken token) { view_creation_token =
  // std::move(token);
  // });
  PRESENT(child, true);
  EXPECT_FALSE(IsDescendantOf(parent->GetRoot(), child->GetRoot()));

  // The same token can be used to link a different instance.
  std::shared_ptr<Flatland> child2 = CreateFlatland();
  child2->CreateView2(std::move(view_creation_token), scenic::NewViewIdentityOnCreation(),
                      NoViewProtocols(), parent_viewport_watcher.NewRequest());
  PRESENT(child2, true);
  EXPECT_TRUE(IsDescendantOf(parent->GetRoot(), child2->GetRoot()));

  // The old instance is not re-linked.
  EXPECT_FALSE(IsDescendantOf(parent->GetRoot(), child->GetRoot()));

  // Both protocols should still be bound.
  EXPECT_TRUE(child_view_watcher.is_bound());
  bool child_view_watcher_updated = false;
  child_view_watcher->GetStatus(
      [&child_view_watcher_updated](auto) { child_view_watcher_updated = true; });
  EXPECT_FALSE(child_view_watcher_updated);
  UpdateLinks(parent->GetRoot());
  RunLoopUntilIdle();
  EXPECT_TRUE(child_view_watcher_updated);

  EXPECT_TRUE(parent_viewport_watcher.is_bound());
  parent_viewport_watcher_updated = false;
  parent_viewport_watcher->GetLayout(
      [&parent_viewport_watcher_updated](auto) { parent_viewport_watcher_updated = true; });
  EXPECT_FALSE(parent_viewport_watcher_updated);
  RunLoopUntilIdle();
  EXPECT_TRUE(parent_viewport_watcher_updated);
}

TEST_F(FlatlandTest, RecreateReleasedLinkSameToken) {
  std::shared_ptr<Flatland> parent = CreateFlatland();
  std::shared_ptr<Flatland> child = CreateFlatland();

  // Create link and Present.
  const ContentId kLinkId1 = {1};
  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  CreateViewport(parent.get(), child.get(), kLinkId1, &child_view_watcher,
                 &parent_viewport_watcher);
  RunLoopUntilIdle();

  const TransformId kId1 = {1};
  parent->CreateTransform(kId1);
  parent->SetRootTransform(kId1);
  parent->SetContent(kId1, kLinkId1);
  PRESENT(parent, true);
  EXPECT_TRUE(IsDescendantOf(parent->GetRoot(), child->GetRoot()));

  // Both protocols should be bound at this point.
  EXPECT_TRUE(child_view_watcher.is_bound());
  bool child_view_watcher_updated = false;
  child_view_watcher->GetStatus(
      [&child_view_watcher_updated](auto) { child_view_watcher_updated = true; });
  EXPECT_FALSE(child_view_watcher_updated);
  UpdateLinks(parent->GetRoot());
  RunLoopUntilIdle();
  EXPECT_TRUE(child_view_watcher_updated);

  EXPECT_TRUE(parent_viewport_watcher.is_bound());

  // Release the link on parent.
  ViewportCreationToken content_token;
  parent->ReleaseViewport(kLinkId1, [&content_token](ViewportCreationToken token) {
    content_token = std::move(token);
  });
  PRESENT(parent, true);
  EXPECT_FALSE(IsDescendantOf(parent->GetRoot(), child->GetRoot()));

  // The same token can be used to create a different link to the same child with a different
  // parent.
  std::shared_ptr<Flatland> parent2 = CreateFlatland();
  const TransformId kId2 = {2};
  parent2->CreateTransform(kId2);
  parent2->SetRootTransform(kId2);
  const ContentId kLinkId2 = {2};
  ViewportProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  parent2->CreateViewport(kLinkId2, std::move(content_token), std::move(properties),
                          child_view_watcher.NewRequest());
  parent2->SetContent(kId2, kLinkId2);
  PRESENT(parent2, true);
  EXPECT_TRUE(IsDescendantOf(parent2->GetRoot(), child->GetRoot()));

  // The old instance is not re-linked.
  EXPECT_FALSE(IsDescendantOf(parent->GetRoot(), child->GetRoot()));

  // Both protocols should still be bound.
  EXPECT_TRUE(child_view_watcher.is_bound());
  child_view_watcher_updated = false;
  child_view_watcher->GetStatus(
      [&child_view_watcher_updated](auto) { child_view_watcher_updated = true; });
  EXPECT_FALSE(child_view_watcher_updated);
  UpdateLinks(parent->GetRoot());
  RunLoopUntilIdle();
  EXPECT_TRUE(child_view_watcher_updated);

  EXPECT_TRUE(parent_viewport_watcher.is_bound());
  bool parent_viewport_watcher_updated = false;
  parent_viewport_watcher->GetLayout(
      [&parent_viewport_watcher_updated](auto) { parent_viewport_watcher_updated = true; });
  EXPECT_FALSE(parent_viewport_watcher_updated);
  RunLoopUntilIdle();
  EXPECT_TRUE(parent_viewport_watcher_updated);
}

TEST_F(FlatlandTest, CreateImageValidCase) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Setup a valid image.
  const ContentId kImageId = {1};
  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  ImageProperties properties;
  properties.set_size({100, 200});

  CreateImage(flatland.get(), allocator.get(), kImageId, std::move(ref_pair),
              std::move(properties));
}

TEST_F(FlatlandTest, CreateImageSetsDefaults) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Setup a valid image.
  const ContentId kImageId = {1};
  const uint32_t kWidth = 100;
  const uint32_t kHeight = 200;
  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  ImageProperties properties;
  properties.set_size({kWidth, kHeight});

  CreateImage(flatland.get(), allocator.get(), kImageId, std::move(ref_pair),
              std::move(properties));

  const auto maybe_image_handle = flatland->GetContentHandle(kImageId);
  ASSERT_TRUE(maybe_image_handle.has_value());
  const auto image_handle = maybe_image_handle.value();
  auto uber_struct = GetUberStruct(flatland.get());

  // Default sample region should be same as size.
  auto sample_region_kv = uber_struct->local_image_sample_regions.find(image_handle);
  EXPECT_NE(sample_region_kv, uber_struct->local_image_sample_regions.end());
  fuchsia::math::RectF rect = {0, 0, kWidth, kHeight};
  ExpectRectFEquals(sample_region_kv->second, rect);

  // Default destination rect should be same as size.
  auto matrix_kv = uber_struct->local_matrices.find(image_handle);
  EXPECT_NE(matrix_kv, uber_struct->local_matrices.end());
  EXPECT_EQ(sample_region_kv->second.width, kWidth);
  EXPECT_EQ(sample_region_kv->second.height, kHeight);
}

TEST_F(FlatlandTest, SetImageOpacityTestCases) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  const TransformId kTransformId = {3};
  const ContentId kId = {1};
  const ContentId kIdChild = {2};

  // Zero is not a valid content ID.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetImageOpacity({0}, 0.5);
    PRESENT(flatland, false);
  }

  // The content id hasn't been imported yet.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetImageOpacity(kId, 0.5);
    PRESENT(flatland, false);
  }

  // Trying to set opacity on a solid color.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateFilledRect(kId);
    flatland->SetImageOpacity(kId, 0.5);
    PRESENT(flatland, false);
  }

  // The alpha values are out of range.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    // Setup a valid image
    BufferCollectionImportExportTokens ref_pair_1 = BufferCollectionImportExportTokens::New();

    ImageProperties properties1;
    properties1.set_size({100, 200});

    auto import_token_dup = ref_pair_1.DuplicateImportToken();
    const allocation::GlobalBufferCollectionId global_collection_id1 =
        CreateImage(flatland.get(), allocator.get(), kId, std::move(ref_pair_1),
                    std::move(properties1))
            .collection_id;

    flatland->CreateTransform(kTransformId);
    flatland->SetRootTransform(kTransformId);
    flatland->SetContent(kTransformId, kId);
    flatland->SetImageOpacity(kId, -0.5);
    PRESENT(flatland, false);
  }
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    // Setup a valid image
    BufferCollectionImportExportTokens ref_pair_1 = BufferCollectionImportExportTokens::New();

    ImageProperties properties1;
    properties1.set_size({100, 200});

    auto import_token_dup = ref_pair_1.DuplicateImportToken();
    const allocation::GlobalBufferCollectionId global_collection_id1 =
        CreateImage(flatland.get(), allocator.get(), kId, std::move(ref_pair_1),
                    std::move(properties1))
            .collection_id;

    flatland->CreateTransform(kTransformId);
    flatland->SetRootTransform(kTransformId);
    flatland->SetContent(kTransformId, kId);
    flatland->SetImageOpacity(kId, 1.5);
    PRESENT(flatland, false);
  }

  // Testing now with good values should finally work.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    // Setup a valid image
    BufferCollectionImportExportTokens ref_pair_1 = BufferCollectionImportExportTokens::New();

    ImageProperties properties1;
    properties1.set_size({100, 200});

    auto import_token_dup = ref_pair_1.DuplicateImportToken();
    const allocation::GlobalBufferCollectionId global_collection_id1 =
        CreateImage(flatland.get(), allocator.get(), kId, std::move(ref_pair_1),
                    std::move(properties1))
            .collection_id;

    flatland->CreateTransform(kTransformId);
    flatland->SetRootTransform(kTransformId);
    flatland->SetContent(kTransformId, kId);
    flatland->SetImageOpacity(kId, 0.7);
    PRESENT(flatland, true);
  }
}

TEST_F(FlatlandTest, SetTransformOpacityTestCases) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  const TransformId kId = {1};
  const TransformId kIdChild = {2};

  // Zero is not a valid transform ID.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetOpacity({0}, 0.5);
    PRESENT(flatland, false);
  }

  // The transform id hasn't been imported yet.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetOpacity(kId, 0.5);
    PRESENT(flatland, false);
  }

  // The alpha values are out of range.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    // Setup a valid transform.
    flatland->CreateTransform(kId);
    flatland->SetRootTransform(kId);
    flatland->SetOpacity(kId, -0.5);
    PRESENT(flatland, false);
  }
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    // Setup a valid transform.
    flatland->CreateTransform(kId);
    flatland->SetRootTransform(kId);
    flatland->SetOpacity(kId, 1.5);
    PRESENT(flatland, false);
  }

  // Testing now with good values should finally work.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    // Setup a valid transform.
    flatland->CreateTransform(kId);
    PRESENT(flatland, true);
    flatland->SetRootTransform(kId);
    flatland->SetOpacity(kId, 0.5);
    PRESENT(flatland, true);
  }
}

TEST_F(FlatlandTest, CreateFilledRectErrorTest) {
  const ContentId kInvalidId = {0};

  // Zero is not a valid content ID.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateFilledRect(kInvalidId);
    PRESENT(flatland, false);
  }

  // Same ID can't be imported twice.
  {
    const ContentId kId = {1};
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateFilledRect(kId);
    PRESENT(flatland, true);

    flatland->CreateFilledRect(kId);
    PRESENT(flatland, false);
  }

  // Test SetSolidFill function.
  {
    const ContentId kId2 = {2};

    // Can't call SetSolidFill on invalid ID.
    {
      std::shared_ptr<Flatland> flatland = CreateFlatland();
      flatland->SetSolidFill(kInvalidId, {1, 0, 0, 1}, {20, 30});
      PRESENT(flatland, false);
    }

    // Can't call SetSolidFill on ID that hasn't been created.
    {
      std::shared_ptr<Flatland> flatland = CreateFlatland();
      flatland->SetSolidFill(kId2, {1, 0, 0, 1}, {20, 30});
      PRESENT(flatland, false);
    }

    // Now it should work after creating the filled rect first.
    {
      std::shared_ptr<Flatland> flatland = CreateFlatland();
      flatland->CreateFilledRect(kId2);
      flatland->SetSolidFill(kId2, {1, 0, 0, 1}, {20, 30});
      PRESENT(flatland, true);
    }

    // Try various values and make sure present still returns true.
    {
      std::shared_ptr<Flatland> flatland = CreateFlatland();
      flatland->CreateFilledRect(kId2);
      flatland->SetSolidFill(kId2, {0.7, 0.3, 0.9, 0.4}, {20, 30});
      PRESENT(flatland, true);
    }
  }

  // Test ReleaseFilledRect function
  {
    const ContentId kId3 = {3};

    // Cannot release an invalid ID.
    {
      std::shared_ptr<Flatland> flatland = CreateFlatland();
      flatland->ReleaseFilledRect(kInvalidId);
      PRESENT(flatland, false);
    }

    // Cannot release an ID that hasn't been created.
    {
      std::shared_ptr<Flatland> flatland = CreateFlatland();
      flatland->ReleaseFilledRect(kId3);
      PRESENT(flatland, false);
    }

    // Now it should work once we create it first.
    {
      std::shared_ptr<Flatland> flatland = CreateFlatland();
      flatland->CreateFilledRect(kId3);
      flatland->ReleaseFilledRect(kId3);
      PRESENT(flatland, true);

      // And now we should be able to reuse the same id.
      flatland->CreateFilledRect(kId3);
      PRESENT(flatland, true);
    }
  }
}

// Make sure that the data for filled rects gets passed along
// correctly to the uberstructs.
TEST_F(FlatlandTest, FilledRectUberstructTest) {
  const ContentId kFilledRectId = {1};
  const ContentId kChildRectId = {3};
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Create constants.
  const uint32_t kFilledWidth = 50;
  const uint32_t kFilledHeight = 100;

  const uint32_t kFilledChildWidth = 75;
  const uint32_t kFilledChildHeight = 220;

  // Create a filled rect and set its color to magenta with a size
  // of (50, 100);
  fuchsia::ui::composition::ColorRgba rect_color = {0.75, 0.5, 0.25, 1.0};
  flatland->CreateFilledRect(kFilledRectId);
  flatland->SetSolidFill(kFilledRectId, rect_color, {kFilledWidth, kFilledHeight});
  PRESENT(flatland, true);

  // Create a second filled rect, set its color to blue, with a size of 75, 220;
  fuchsia::ui::composition::ColorRgba child_color = {0.50, 0.75, 1.0, 0.25};
  flatland->CreateFilledRect(kChildRectId);
  flatland->SetSolidFill(kChildRectId, child_color, {kFilledChildWidth, kFilledChildHeight});
  PRESENT(flatland, true);

  // Create the transform graph. We will have a root node with one rectangle as content,
  // a child node, and a second rectangle as content on the child node.
  const TransformId kTransformId = {2};
  const TransformId kChildTransformId = {4};

  // Create both transforms.
  flatland->CreateTransform(kTransformId);
  flatland->CreateTransform(kChildTransformId);

  // Set the root of the tree, and set the content on that root.
  flatland->SetRootTransform(kTransformId);
  flatland->SetContent(kTransformId, kFilledRectId);

  // Add the child transform to the parent transform, and set the child rectangle as
  // content on the child transform.
  flatland->AddChild(kTransformId, kChildTransformId);
  flatland->SetContent(kChildTransformId, kChildRectId);

  PRESENT(flatland, true);

  // Get the filled rect content handle.
  const auto maybe_rect_handle = flatland->GetContentHandle(kFilledRectId);
  ASSERT_TRUE(maybe_rect_handle.has_value());
  const auto rect_handle = maybe_rect_handle.value();

  // Get the filled child rect handle.
  const auto maybe_child_rect_handle = flatland->GetContentHandle(kChildRectId);
  ASSERT_TRUE(maybe_child_rect_handle.has_value());
  const auto child_rect_handle = maybe_child_rect_handle.value();

  // Now find the data for both rectangles in the uber struct. The last handle
  // should be that of the child rectangle.
  auto uber_struct = GetUberStruct(flatland.get());
  EXPECT_EQ(uber_struct->local_topology.back().handle, child_rect_handle);

  // Grab the metadata for each handle.
  auto image_kv = uber_struct->images.find(rect_handle);
  EXPECT_NE(image_kv, uber_struct->images.end());
  auto child_image_kv = uber_struct->images.find(child_rect_handle);
  EXPECT_NE(child_image_kv, uber_struct->images.end());

  // Make sure the color for each rectangle matches the above colors.
  EXPECT_EQ(image_kv->second.multiply_color[0], rect_color.red);
  EXPECT_EQ(image_kv->second.multiply_color[1], rect_color.green);
  EXPECT_EQ(image_kv->second.multiply_color[2], rect_color.blue);
  EXPECT_EQ(image_kv->second.multiply_color[3], rect_color.alpha);

  EXPECT_EQ(child_image_kv->second.multiply_color[0], child_color.red);
  EXPECT_EQ(child_image_kv->second.multiply_color[1], child_color.green);
  EXPECT_EQ(child_image_kv->second.multiply_color[2], child_color.blue);
  EXPECT_EQ(child_image_kv->second.multiply_color[3], child_color.alpha);

  // Grab the data for the matrices.
  auto matrix_kv = uber_struct->local_matrices.find(rect_handle);
  EXPECT_NE(matrix_kv, uber_struct->local_matrices.end());

  auto child_matrix_kv = uber_struct->local_matrices.find(child_rect_handle);
  EXPECT_NE(child_matrix_kv, uber_struct->local_matrices.end());

  // Make sure the values match.
  EXPECT_EQ(static_cast<uint32_t>(matrix_kv->second[0][0]), kFilledWidth);
  EXPECT_EQ(static_cast<uint32_t>(matrix_kv->second[1][1]), kFilledHeight);
  EXPECT_EQ(static_cast<uint32_t>(child_matrix_kv->second[0][0]), kFilledChildWidth);
  EXPECT_EQ(static_cast<uint32_t>(child_matrix_kv->second[1][1]), kFilledChildHeight);
}

TEST_F(FlatlandTest, SetImageSampleRegionTestCases) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  const TransformId kTransformId = {1};
  const ContentId kId = {3};
  const uint32_t kImageWidth = 300;
  const uint32_t kImageHeight = 400;

  // Zero is not a valid content ID.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetImageSampleRegion({0}, {0, 0, 100, 200});
    PRESENT(flatland, false);
  }

  // The content id hasn't been imported yet.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetImageSampleRegion(kId, {0, 0, 100, 200});
    PRESENT(flatland, false);
  }

  // Setup a valid transform and image.
  std::shared_ptr<Flatland> flatland = CreateFlatland();
  flatland->CreateTransform(kTransformId);
  flatland->SetRootTransform(kTransformId);

  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  ImageProperties properties;
  properties.set_size({kImageWidth, kImageHeight});

  CreateImage(flatland.get(), allocator.get(), kId, std::move(ref_pair), std::move(properties));

  flatland->SetContent(kTransformId, kId);
  PRESENT(flatland, true);

  // Testing now with good values should finally work.
  {
    flatland->SetImageSampleRegion(kId, {0, 0, kImageWidth, kImageHeight});
    PRESENT(flatland, true);

    flatland->SetImageSampleRegion(kId, {50, 60, kImageWidth - 100, kImageHeight - 200});
    PRESENT(flatland, true);
  }

  // Test one more time with out of bounds values and it should fail.
  {
    // (x + width) exceeeds the image width and should fail.
    flatland->SetImageSampleRegion(kId, {1, 0, kImageWidth, kImageHeight});
    PRESENT(flatland, false);
  }
}

TEST_F(FlatlandTest, SetClipBoundaryErrorCases) {
  const TransformId kTransformId = {1};

  // Zero is not a valid transform ID.
  {
    fuchsia::math::Rect rect = {0, 0, 20, 30};
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetClipBoundary({0}, std::make_unique<fuchsia::math::Rect>(std::move(rect)));
    PRESENT(flatland, false);
  }

  // Transform ID is valid but not yet imported
  {
    fuchsia::math::Rect rect = {0, 0, 20, 30};
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetClipBoundary(kTransformId, std::make_unique<fuchsia::math::Rect>(std::move(rect)));
    PRESENT(flatland, false);
  }

  // Width must be positive.
  {
    fuchsia::math::Rect rect = {0, 0, 20, 30};
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateTransform(kTransformId);
    flatland->SetRootTransform(kTransformId);
    flatland->SetClipBoundary(kTransformId, std::make_unique<fuchsia::math::Rect>(std::move(rect)));
    PRESENT(flatland, true);

    const auto maybe_transform_handle = flatland->GetTransformHandle(kTransformId);
    ASSERT_TRUE(maybe_transform_handle.has_value());
    const auto transform_handle = maybe_transform_handle.value();

    auto uber_struct = GetUberStruct(flatland.get());
    auto clip_region_itr = uber_struct->local_clip_regions.find(transform_handle);
    EXPECT_NE(clip_region_itr, uber_struct->local_clip_regions.end());
    auto clip_region = clip_region_itr->second;
    EXPECT_EQ(rect.x, clip_region.x);
    EXPECT_EQ(rect.y, clip_region.y);
    EXPECT_EQ(rect.width, clip_region.width);
    EXPECT_EQ(rect.height, clip_region.height);

    fuchsia::math::Rect rect_bad = {0, 0, -20, 30};
    flatland->SetClipBoundary(kTransformId,
                              std::make_unique<fuchsia::math::Rect>(std::move(rect_bad)));
    PRESENT(flatland, false);
  }

  // Height must be positive.
  {
    fuchsia::math::Rect rect = {0, 0, 20, 30};
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateTransform(kTransformId);
    flatland->SetClipBoundary(kTransformId, std::make_unique<fuchsia::math::Rect>(std::move(rect)));
    PRESENT(flatland, true);

    fuchsia::math::Rect rect_bad = {0, 0, 20, -30};
    flatland->SetClipBoundary(kTransformId,
                              std::make_unique<fuchsia::math::Rect>(std::move(rect_bad)));
    PRESENT(flatland, false);
  }

  // Can't overflow on the X-axis.
  {
    fuchsia::math::Rect rect = {INT_MAX - 1, 0, INT_MAX - 1, 30};
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateTransform(kTransformId);
    flatland->SetClipBoundary(kTransformId, std::make_unique<fuchsia::math::Rect>(std::move(rect)));
    PRESENT(flatland, false);
  }

  // Can't overflow on the Y-axis.
  {
    fuchsia::math::Rect rect = {0, INT_MAX - 1, 30, INT_MAX - 1};
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateTransform(kTransformId);
    flatland->SetClipBoundary(kTransformId, std::make_unique<fuchsia::math::Rect>(std::move(rect)));
    PRESENT(flatland, false);
  }

  // Null value is OK.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateTransform(kTransformId);
    flatland->SetRootTransform(kTransformId);

    // Grab the transform handle.
    const auto maybe_transform_handle = flatland->GetTransformHandle(kTransformId);
    ASSERT_TRUE(maybe_transform_handle.has_value());
    const auto transform_handle = maybe_transform_handle.value();

    // Set a null value.
    flatland->SetClipBoundary(kTransformId, nullptr);
    PRESENT(flatland, true);

    // Check that there is no clip region in the uber struct.
    auto uber_struct = GetUberStruct(flatland.get());
    auto clip_region_itr = uber_struct->local_clip_regions.find(transform_handle);
    EXPECT_EQ(clip_region_itr, uber_struct->local_clip_regions.end());

    // Set a proper value.
    fuchsia::math::Rect rect = {10, 30, 20, 90};
    flatland->SetClipBoundary(kTransformId, std::make_unique<fuchsia::math::Rect>(std::move(rect)));
    PRESENT(flatland, true);

    // Check that this value has now made its way to the uber struct.
    uber_struct = GetUberStruct(flatland.get());
    clip_region_itr = uber_struct->local_clip_regions.find(transform_handle);
    EXPECT_NE(clip_region_itr, uber_struct->local_clip_regions.end());
    auto clip_region = clip_region_itr->second;
    EXPECT_EQ(rect.x, clip_region.x);
    EXPECT_EQ(rect.y, clip_region.y);
    EXPECT_EQ(rect.width, clip_region.width);
    EXPECT_EQ(rect.height, clip_region.height);

    // Set it to be null again.
    flatland->SetClipBoundary(kTransformId, nullptr);
    PRESENT(flatland, true);

    // Now check that its not in the uber struct anymore.
    uber_struct = GetUberStruct(flatland.get());
    clip_region_itr = uber_struct->local_clip_regions.find(transform_handle);
    EXPECT_EQ(clip_region_itr, uber_struct->local_clip_regions.end());
  }
}

TEST_F(FlatlandTest, CreateImageErrorCases) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();

  // Default image properties.
  const uint32_t kDefaultVmoIndex = 1;
  const uint32_t kDefaultWidth = 100;
  const uint32_t kDefaultHeight = 1000;

  // Setup a valid buffer collection.
  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), true);

  // Zero is not a valid image ID.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateImage({0}, ref_pair.DuplicateImportToken(), kDefaultVmoIndex,
                          ImageProperties());
    PRESENT(flatland, false);
  }

  // The import token must also be valid.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateImage({1}, BufferCollectionImportToken(), kDefaultVmoIndex, ImageProperties());
    PRESENT(flatland, false);
  }

  // The buffer collection can fail to create an image.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateImage({1}, ref_pair.DuplicateImportToken(), kDefaultVmoIndex,
                          ImageProperties());
    PRESENT(flatland, false);
  }

  // Size must be set.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->CreateImage({1}, ref_pair.DuplicateImportToken(), kDefaultVmoIndex,
                          ImageProperties());
    PRESENT(flatland, false);
  }

  // Width cannot be 0.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    ImageProperties properties;
    properties.set_size({0, 1});
    flatland->CreateImage({1}, ref_pair.DuplicateImportToken(), kDefaultVmoIndex,
                          std::move(properties));
    PRESENT(flatland, false);
  }

  // Height cannot be 0.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    ImageProperties properties;
    properties.set_size({1, 0});
    flatland->CreateImage({1}, ref_pair.DuplicateImportToken(), kDefaultVmoIndex,
                          std::move(properties));
    PRESENT(flatland, false);
  }

  // Check to make sure that if the BufferCollectionImporter returns false, then the call
  // to Flatland::CreateImage() also returns false.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    const ContentId kId = {100};
    ImageProperties properties;
    properties.set_size({kDefaultWidth, kDefaultHeight});
    EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _)).WillOnce(Return(false));
    flatland->CreateImage(kId, ref_pair.DuplicateImportToken(), kDefaultVmoIndex,
                          std::move(properties));
    PRESENT(flatland, false);
  }

  // Two images cannot have the same ID.
  const ContentId kId = {1};
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    {
      ImageProperties properties;
      properties.set_size({kDefaultWidth, kDefaultHeight});

      // This is the first call in these series of test components that makes it down to
      // the BufferCollectionImporter. We have to make sure it returns true here so that
      // the test doesn't erroneously fail.
      EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _))
          .WillOnce(Return(true));

      flatland->CreateImage(kId, ref_pair.DuplicateImportToken(), kDefaultVmoIndex,
                            std::move(properties));
      PRESENT(flatland, true);
    }

    {
      ImageProperties properties;
      properties.set_size({kDefaultWidth, kDefaultHeight});

      // We shouldn't even make it to the BufferCollectionImporter here due to the duplicate
      // ID causing CreateImage() to return early.
      EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _)).Times(0);
      flatland->CreateImage(kId, ref_pair.DuplicateImportToken(), kDefaultVmoIndex,
                            std::move(properties));
      PRESENT(flatland, false);
    }
  }

  // A Link id cannot be used for an image.
  const ContentId kLinkId = {2};
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    ViewportCreationToken parent_token;
    ViewCreationToken child_token;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
    ViewportProperties link_properties;
    link_properties.set_logical_size({kDefaultSize, kDefaultSize});
    flatland->CreateViewport(kLinkId, std::move(parent_token), std::move(link_properties),
                             child_view_watcher.NewRequest());
    PRESENT(flatland, true);

    ImageProperties image_properties;
    image_properties.set_size({kDefaultWidth, kDefaultHeight});

    flatland->CreateImage(kLinkId, ref_pair.DuplicateImportToken(), kDefaultVmoIndex,
                          std::move(image_properties));
    PRESENT(flatland, false);
  }
}

TEST_F(FlatlandTest, CreateImageWithDuplicatedImportTokens) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), true);

  const uint64_t kNumImages = 3;
  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _))
      .Times(kNumImages)
      .WillRepeatedly(Return(true));

  for (uint64_t i = 0; i < kNumImages; ++i) {
    ImageProperties properties;
    properties.set_size({150, 175});
    flatland->CreateImage(/*image_id*/ {i + 1}, ref_pair.DuplicateImportToken(), /*vmo_idx*/ i,
                          std::move(properties));
    PRESENT(flatland, true);
  }
}

TEST_F(FlatlandTest, CreateImageInMultipleFlatlands) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  std::shared_ptr<Flatland> flatland1 = CreateFlatland();
  std::shared_ptr<Flatland> flatland2 = CreateFlatland();

  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), true);

  // We can import the same image in both flatland instances.
  {
    EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _)).WillOnce(Return(true));
    ImageProperties properties;
    properties.set_size({150, 175});
    flatland1->CreateImage({1}, ref_pair.DuplicateImportToken(), 0, std::move(properties));
    PRESENT(flatland1, true);
  }
  {
    EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _)).WillOnce(Return(true));
    ImageProperties properties;
    properties.set_size({150, 175});
    flatland2->CreateImage({1}, ref_pair.DuplicateImportToken(), 0, std::move(properties));
    PRESENT(flatland2, true);
  }

  // There are seperate ReleaseBufferImage calls to release them from importers.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(2);
  flatland1->Clear();
  PRESENT(flatland1, true);
  flatland2->Clear();
  PRESENT(flatland2, true);
}

TEST_F(FlatlandTest, SetContentErrorCases) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Setup a valid image.
  const ContentId kImageId = {1};
  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  const uint32_t kWidth = 100;
  const uint32_t kHeight = 200;

  ImageProperties properties;
  properties.set_size({kWidth, kHeight});

  CreateImage(flatland.get(), allocator.get(), kImageId, std::move(ref_pair),
              std::move(properties));

  // Create a transform.
  const TransformId kTransformId = {1};

  flatland->CreateTransform(kTransformId);
  PRESENT(flatland, true);

  // Zero is not a valid transform.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetContent({0}, kImageId);
    PRESENT(flatland, false);
  }

  // The transform must exist.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetContent({2}, kImageId);
    PRESENT(flatland, false);
  }

  // The image must exist.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->SetContent(kTransformId, {2});
    PRESENT(flatland, false);
  }
}

TEST_F(FlatlandTest, ClearContentOnTransform) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Setup a valid image.
  const ContentId kImageId = {1};
  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();

  ImageProperties properties;
  properties.set_size({100, 200});

  auto import_token_dup = ref_pair.DuplicateImportToken();
  auto global_collection_id = CreateImage(flatland.get(), allocator.get(), kImageId,
                                          std::move(ref_pair), std::move(properties))
                                  .collection_id;

  const auto maybe_image_handle = flatland->GetContentHandle(kImageId);
  ASSERT_TRUE(maybe_image_handle.has_value());
  const auto image_handle = maybe_image_handle.value();

  // Create a transform, make it the root transform, and attach the image.
  const TransformId kTransformId = {1};

  flatland->CreateTransform(kTransformId);
  flatland->SetRootTransform(kTransformId);
  flatland->SetContent(kTransformId, kImageId);
  PRESENT(flatland, true);

  // The image handle should be the last handle in the local_topology, and the image should be in
  // the image map.
  auto uber_struct = GetUberStruct(flatland.get());
  EXPECT_EQ(uber_struct->local_topology.back().handle, image_handle);

  auto image_kv = uber_struct->images.find(image_handle);
  EXPECT_NE(image_kv, uber_struct->images.end());
  EXPECT_EQ(image_kv->second.collection_id, global_collection_id);

  // An ContentId of 0 indicates to remove any content on the specified transform.
  flatland->SetContent(kTransformId, {0});
  PRESENT(flatland, true);

  uber_struct = GetUberStruct(flatland.get());
  for (const auto& entry : uber_struct->local_topology) {
    EXPECT_NE(entry.handle, image_handle);
  }
}

TEST_F(FlatlandTest, SetTheSameContentOnMultipleTransforms) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Setup a valid image.
  const ContentId kImageId = {1};
  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  ImageProperties properties;
  properties.set_size({100, 200});
  auto import_token_dup = ref_pair.DuplicateImportToken();
  CreateImage(flatland.get(), allocator.get(), kImageId, std::move(ref_pair),
              std::move(properties));

  // Create a transform, make it the root transform, and add two children.
  const TransformId kTransformId1 = {1};
  const TransformId kTransformId2 = {2};
  const TransformId kTransformId3 = {3};

  flatland->CreateTransform(kTransformId1);
  flatland->CreateTransform(kTransformId2);
  flatland->CreateTransform(kTransformId3);

  flatland->SetRootTransform(kTransformId1);
  flatland->AddChild(kTransformId1, kTransformId2);
  flatland->AddChild(kTransformId1, kTransformId3);

  // Set the same content on both children
  flatland->SetContent(kTransformId2, kImageId);
  flatland->SetContent(kTransformId3, kImageId);
  PRESENT(flatland, true);
}

TEST_F(FlatlandTest, TopologyVisitsContentBeforeChildren) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Setup two valid images.
  const ContentId kImageId1 = {1};
  BufferCollectionImportExportTokens ref_pair_1 = BufferCollectionImportExportTokens::New();

  ImageProperties properties1;
  properties1.set_size({100, 200});

  CreateImage(flatland.get(), allocator.get(), kImageId1, std::move(ref_pair_1),
              std::move(properties1));

  const auto maybe_image_handle1 = flatland->GetContentHandle(kImageId1);
  ASSERT_TRUE(maybe_image_handle1.has_value());
  const auto image_handle1 = maybe_image_handle1.value();

  const ContentId kImageId2 = {2};
  BufferCollectionImportExportTokens ref_pair_2 = BufferCollectionImportExportTokens::New();

  ImageProperties properties2;
  properties2.set_size({300, 400});

  CreateImage(flatland.get(), allocator.get(), kImageId2, std::move(ref_pair_2),
              std::move(properties2));

  const auto maybe_image_handle2 = flatland->GetContentHandle(kImageId2);
  ASSERT_TRUE(maybe_image_handle2.has_value());
  const auto image_handle2 = maybe_image_handle2.value();

  // Create a root transform with two children.
  const TransformId kTransformId1 = {3};
  const TransformId kTransformId2 = {4};
  const TransformId kTransformId3 = {5};

  flatland->CreateTransform(kTransformId1);
  flatland->CreateTransform(kTransformId2);
  flatland->CreateTransform(kTransformId3);

  flatland->AddChild(kTransformId1, kTransformId2);
  flatland->AddChild(kTransformId1, kTransformId3);

  flatland->SetRootTransform(kTransformId1);
  PRESENT(flatland, true);

  // Attach image 1 to the root and the second child-> Attach image 2 to the first child->
  flatland->SetContent(kTransformId1, kImageId1);
  flatland->SetContent(kTransformId2, kImageId2);
  flatland->SetContent(kTransformId3, kImageId1);
  PRESENT(flatland, true);

  // The images should appear pre-order toplogically sorted: 1, 2, 1 again. The same image is
  // allowed to appear multiple times.
  std::queue<TransformHandle> expected_handle_order;
  expected_handle_order.push(image_handle1);
  expected_handle_order.push(image_handle2);
  expected_handle_order.push(image_handle1);
  auto uber_struct = GetUberStruct(flatland.get());
  for (const auto& entry : uber_struct->local_topology) {
    if (entry.handle == expected_handle_order.front()) {
      expected_handle_order.pop();
    }
  }
  EXPECT_TRUE(expected_handle_order.empty());

  // Clearing the image from the parent removes the first entry of the list since images are
  // visited before children.
  flatland->SetContent(kTransformId1, {0});
  PRESENT(flatland, true);

  // Meaning the new list of images should be: 2, 1.
  expected_handle_order.push(image_handle2);
  expected_handle_order.push(image_handle1);
  uber_struct = GetUberStruct(flatland.get());
  for (const auto& entry : uber_struct->local_topology) {
    if (entry.handle == expected_handle_order.front()) {
      expected_handle_order.pop();
    }
  }
  EXPECT_TRUE(expected_handle_order.empty());
}

// Tests that a buffer collection is released after CreateImage() if there are no more import
// tokens.
TEST_F(FlatlandTest, ReleaseBufferCollectionHappensAfterCreateImage) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Register a valid buffer collection.
  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), true);

  const ContentId kImageId = {1};
  ImageProperties properties;
  properties.set_size({100, 200});

  // Send our only import token to CreateImage(). Buffer collection should be released only after
  // Image creation.
  {
    EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _)).WillOnce(Return(true));
    EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(_, _)).Times(1);
    flatland->CreateImage(kImageId, std::move(ref_pair.import_token), 0, std::move(properties));
    RunLoopUntilIdle();
  }
}

TEST_F(FlatlandTest, ReleaseBufferCollectionCompletesAfterFlatlandDestruction) {
  allocation::GlobalBufferCollectionId global_collection_id;
  ContentId global_image_id;
  {
    std::shared_ptr<Allocator> allocator = CreateAllocator();
    std::shared_ptr<Flatland> flatland = CreateFlatland();

    const ContentId kImageId = {3};
    BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
    ImageProperties properties;
    properties.set_size({200, 200});
    auto import_token_dup = ref_pair.DuplicateImportToken();
    auto global_id_pair = CreateImage(flatland.get(), allocator.get(), kImageId,
                                      std::move(ref_pair), std::move(properties));
    global_collection_id = global_id_pair.collection_id;
    global_image_id = {global_id_pair.image_id};

    // Release the image.
    flatland->ReleaseImage(kImageId);

    // Release the buffer collection.

    EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(global_collection_id, _))
        .Times(1);
    import_token_dup.value.reset();
    RunLoopUntilIdle();

    // Skip session updates to test that release fences are what trigger the importer calls.
    EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(global_image_id.value))
        .Times(0);
    PresentArgs args;
    args.skip_session_update_and_release_fences = true;
    { PRESENT_WITH_ARGS(flatland, std::move(args), true); }

    // |flatland| falls out of scope.
  }

  // Reset the last known reference to the BufferImporter to demonstrate that the Wait keeps it
  // alive.
  buffer_collection_importer_.reset();

  // Signal the release fences, which triggers the release call, even though the Flatland
  // instance and BufferCollectionImporter associated with the call have been cleaned up.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(global_image_id.value))
      .Times(1);
  ApplySessionUpdatesAndSignalFences();
  RunLoopUntilIdle();
}

// Tests that an Image is not released from the importer until it is not referenced and the
// release fence is signaled.
TEST_F(FlatlandTest, ReleaseImageWaitsForReleaseFence) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Setup a valid buffer collection and Image.
  const ContentId kImageId = {1};
  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();

  ImageProperties properties;
  properties.set_size({100, 200});

  auto import_token_dup = ref_pair.DuplicateImportToken();
  const auto global_id_pair = CreateImage(flatland.get(), allocator.get(), kImageId,
                                          std::move(ref_pair), std::move(properties));
  auto& global_collection_id = global_id_pair.collection_id;

  // Attach the Image to a transform.
  const TransformId kTransformId = {3};
  flatland->CreateTransform(kTransformId);
  flatland->SetRootTransform(kTransformId);
  flatland->SetContent(kTransformId, kImageId);
  PRESENT(flatland, true);

  // Release the buffer collection, but ensure that the ReleaseBufferImage call on the importer
  // has not happened.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(global_collection_id, _))
      .Times(1);
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(0);
  import_token_dup.value.reset();
  RunLoopUntilIdle();

  // Release the Image that referenced the buffer collection. Because the Image is still attached
  // to a Transform, the deregestration call should still not happen.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(0);
  flatland->ReleaseImage(kImageId);
  PRESENT(flatland, true);

  // Remove the Image from the transform. This triggers the creation of the release fence, but
  // still does not result in a deregestration call. Skip session updates to test that release
  // fences are what trigger the importer calls.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(0);
  flatland->SetContent(kTransformId, {0});

  PresentArgs args;
  args.skip_session_update_and_release_fences = true;
  PRESENT_WITH_ARGS(flatland, std::move(args), true);

  // Signal the release fences, which triggers the release call.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(1);
  ApplySessionUpdatesAndSignalFences();
  RunLoopUntilIdle();
}

TEST_F(FlatlandTest, ReleaseImageErrorCases) {
  // Zero is not a valid image ID.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->ReleaseImage({0});
    PRESENT(flatland, false);
  }

  // The image must exist.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    flatland->ReleaseImage({1});
    PRESENT(flatland, false);
  }

  // ContentId is not an Image.
  {
    std::shared_ptr<Flatland> flatland = CreateFlatland();
    ViewportCreationToken parent_token;
    ViewCreationToken child_token;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &parent_token.value, &child_token.value));

    const ContentId kLinkId = {2};

    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
    ViewportProperties properties;
    properties.set_logical_size({kDefaultSize, kDefaultSize});
    flatland->CreateViewport(kLinkId, std::move(parent_token), std::move(properties),
                             child_view_watcher.NewRequest());

    flatland->ReleaseImage(kLinkId);
    PRESENT(flatland, false);
  }
}

// If we have multiple BufferCollectionImporters, some of them may properly import
// an image while others do not. We have to therefore make sure that if importer A
// properly imports an image and then importer B fails, that Flatland automatically
// releases the image from importer A.
TEST_F(FlatlandTest, ImageImportPassesAndFailsOnDifferentImportersTest) {
  // Create a second buffer collection importer.
  auto local_mock_buffer_collection_importer = new MockBufferCollectionImporter();
  auto local_buffer_collection_importer =
      std::shared_ptr<allocation::BufferCollectionImporter>(local_mock_buffer_collection_importer);

  // Create flatland and allocator instances that has two BufferCollectionImporters.
  std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> importers(
      {buffer_collection_importer_, local_buffer_collection_importer});
  std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> screenshot_importers;
  std::shared_ptr<Allocator> allocator = std::make_shared<Allocator>(
      context_provider_.context(), importers, screenshot_importers,
      utils::CreateSysmemAllocatorSyncPtr("ImageImportPassesFailsOnDiffImportersTest"));
  auto session_id = scheduling::GetNextSessionId();
  fuchsia::ui::composition::FlatlandPtr flatland_ptr;
  auto flatland = Flatland::New(
      std::make_shared<utils::UnownedDispatcherHolder>(dispatcher()), flatland_ptr.NewRequest(),
      session_id,
      /*destroy_instance_functon=*/[]() {}, flatland_presenter_, link_system_,
      uber_struct_system_->AllocateQueueForSession(session_id), importers, [](auto...) {},
      [](auto...) {}, [](auto...) {}, [](auto...) {});
  EXPECT_CALL(*local_mock_buffer_collection_importer, ImportBufferCollection(_, _, _, _, _))
      .WillOnce(Return(true));

  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), true);

  ImageProperties properties;
  properties.set_size({100, 200});

  // We have the first importer return true, signifying a successful import, and the second one
  // returning false. This should trigger the first importer to call ReleaseBufferImage().
  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*local_mock_buffer_collection_importer, ImportBufferImage(_, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).WillOnce(Return());
  flatland->CreateImage(/*image_id*/ {1}, std::move(ref_pair.import_token), /*vmo_idx*/ 0,
                        std::move(properties));
}

// Test to make sure that if a buffer collection importer returns |false|
// on |ImportBufferImage()| that this is caught when we try to present.
TEST_F(FlatlandTest, BufferImporterImportImageReturnsFalseTest) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();
  REGISTER_BUFFER_COLLECTION(allocator, ref_pair.export_token, CreateToken(), true);

  // Create a proper properties struct.
  ImageProperties properties1;
  properties1.set_size({150, 175});

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _)).WillOnce(Return(true));

  // We've imported a proper image and we have the importer returning true, so
  // PRESENT should return true.
  flatland->CreateImage(/*image_id*/ {1}, ref_pair.DuplicateImportToken(), /*vmo_idx*/ 0,
                        std::move(properties1));
  PRESENT(flatland, true);

  // We're using the same buffer collection so we don't need to validate, only import.
  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _)).WillOnce(Return(false));

  // Import again, but this time have the importer return false. Flatland should catch
  // this and PRESENT should return false.
  ImageProperties properties2;
  properties2.set_size({150, 175});
  flatland->CreateImage(/*image_id*/ {2}, ref_pair.DuplicateImportToken(), /*vmo_idx*/ 0,
                        std::move(properties2));
  PRESENT(flatland, false);
}

// Test to make sure that the release fences signal to the buffer importer
// to release the image.
TEST_F(FlatlandTest, BufferImporterImageReleaseTest) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Setup a valid image.
  const ContentId kImageId = {1};
  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();

  ImageProperties properties1;
  properties1.set_size({100, 200});

  const allocation::GlobalBufferCollectionId global_collection_id1 =
      CreateImage(flatland.get(), allocator.get(), kImageId, std::move(ref_pair),
                  std::move(properties1))
          .collection_id;

  // Create a transform, make it the root transform, and attach the image.
  const TransformId kTransformId = {2};

  flatland->CreateTransform(kTransformId);
  flatland->SetRootTransform(kTransformId);
  flatland->SetContent(kTransformId, kImageId);
  PRESENT(flatland, true);

  // Now release the image.
  flatland->ReleaseImage(kImageId);
  PRESENT(flatland, true);

  // Now remove the image from the transform, which should result in it being
  // garbage collected.
  flatland->SetContent(kTransformId, {0});
  PresentArgs args;
  args.skip_session_update_and_release_fences = true;
  PRESENT_WITH_ARGS(flatland, std::move(args), true);

  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(1);
  ApplySessionUpdatesAndSignalFences();
  RunLoopUntilIdle();
}

TEST_F(FlatlandTest, ReleasedImageRemainsUntilCleared) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Setup a valid image.
  const ContentId kImageId = {1};
  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();

  ImageProperties properties1;
  properties1.set_size({100, 200});

  const allocation::GlobalBufferCollectionId global_collection_id =
      CreateImage(flatland.get(), allocator.get(), kImageId, std::move(ref_pair),
                  std::move(properties1))
          .collection_id;

  const auto maybe_image_handle = flatland->GetContentHandle(kImageId);
  ASSERT_TRUE(maybe_image_handle.has_value());
  const auto image_handle = maybe_image_handle.value();

  // Create a transform, make it the root transform, and attach the image.
  const TransformId kTransformId = {2};

  flatland->CreateTransform(kTransformId);
  flatland->SetRootTransform(kTransformId);
  flatland->SetContent(kTransformId, kImageId);
  PRESENT(flatland, true);

  // The image handle should be the last handle in the local_topology, and the image should be in
  // the image map.
  auto uber_struct = GetUberStruct(flatland.get());
  EXPECT_EQ(uber_struct->local_topology.back().handle, image_handle);

  auto image_kv = uber_struct->images.find(image_handle);
  EXPECT_NE(image_kv, uber_struct->images.end());
  EXPECT_EQ(image_kv->second.collection_id, global_collection_id);

  // Releasing the image succeeds, but all data remains in the UberStruct.
  flatland->ReleaseImage(kImageId);
  PRESENT(flatland, true);

  uber_struct = GetUberStruct(flatland.get());
  EXPECT_EQ(uber_struct->local_topology.back().handle, image_handle);

  image_kv = uber_struct->images.find(image_handle);
  EXPECT_NE(image_kv, uber_struct->images.end());
  EXPECT_EQ(image_kv->second.collection_id, global_collection_id);

  // Clearing the Transform of its Image removes all references from the UberStruct.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(1);
  flatland->SetContent(kTransformId, {0});
  PRESENT(flatland, true);

  uber_struct = GetUberStruct(flatland.get());
  for (const auto& entry : uber_struct->local_topology) {
    EXPECT_NE(entry.handle, image_handle);
  }

  EXPECT_FALSE(uber_struct->images.count(image_handle));
}

TEST_F(FlatlandTest, ReleasedImageIdCanBeReused) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Setup a valid image.
  const ContentId kImageId = {1};
  BufferCollectionImportExportTokens ref_pair_1 = BufferCollectionImportExportTokens::New();

  ImageProperties properties1;
  properties1.set_size({100, 200});

  const allocation::GlobalBufferCollectionId global_collection_id1 =
      CreateImage(flatland.get(), allocator.get(), kImageId, std::move(ref_pair_1),
                  std::move(properties1))
          .collection_id;

  const auto maybe_image_handle1 = flatland->GetContentHandle(kImageId);
  ASSERT_TRUE(maybe_image_handle1.has_value());
  const auto image_handle1 = maybe_image_handle1.value();

  // Create a transform, make it the root transform, attach the image, then release it.
  const TransformId kTransformId1 = {2};

  flatland->CreateTransform(kTransformId1);
  flatland->SetRootTransform(kTransformId1);
  flatland->SetContent(kTransformId1, kImageId);
  flatland->ReleaseImage(kImageId);
  PRESENT(flatland, true);

  // The ContentId can be re-used even though the old image is still present. Add a second
  // transform so that both images show up in the global image vector.
  BufferCollectionImportExportTokens ref_pair_2 = BufferCollectionImportExportTokens::New();
  ImageProperties properties2;
  properties2.set_size({300, 400});

  const allocation::GlobalBufferCollectionId global_collection_id2 =
      CreateImage(flatland.get(), allocator.get(), kImageId, std::move(ref_pair_2),
                  std::move(properties2))
          .collection_id;

  const TransformId kTransformId2 = {3};

  flatland->CreateTransform(kTransformId2);
  flatland->AddChild(kTransformId1, kTransformId2);
  flatland->SetContent(kTransformId2, kImageId);
  PRESENT(flatland, true);

  const auto maybe_image_handle2 = flatland->GetContentHandle(kImageId);
  ASSERT_TRUE(maybe_image_handle2.has_value());
  const auto image_handle2 = maybe_image_handle2.value();

  // Both images should appear in the image map.
  auto uber_struct = GetUberStruct(flatland.get());

  auto image_kv1 = uber_struct->images.find(image_handle1);
  EXPECT_NE(image_kv1, uber_struct->images.end());
  EXPECT_EQ(image_kv1->second.collection_id, global_collection_id1);

  auto image_kv2 = uber_struct->images.find(image_handle2);
  EXPECT_NE(image_kv2, uber_struct->images.end());
  EXPECT_EQ(image_kv2->second.collection_id, global_collection_id2);
}

// Test that released Images, when attached to a Transform, are not garbage collected even if
// the Transform is not part of the most recently presented global topology.
TEST_F(FlatlandTest, ReleasedImagePersistsOutsideGlobalTopology) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Setup a valid image.
  const ContentId kImageId = {1};
  BufferCollectionImportExportTokens ref_pair = BufferCollectionImportExportTokens::New();

  ImageProperties properties1;
  properties1.set_size({100, 200});

  const allocation::GlobalBufferCollectionId global_collection_id1 =
      CreateImage(flatland.get(), allocator.get(), kImageId, std::move(ref_pair),
                  std::move(properties1))
          .collection_id;

  const auto maybe_image_handle = flatland->GetContentHandle(kImageId);
  ASSERT_TRUE(maybe_image_handle.has_value());
  const auto image_handle = maybe_image_handle.value();

  // Create a transform, make it the root transform, attach the image, then release it.
  const TransformId kTransformId = {2};

  flatland->CreateTransform(kTransformId);
  flatland->SetRootTransform(kTransformId);
  flatland->SetContent(kTransformId, kImageId);
  flatland->ReleaseImage(kImageId);
  PRESENT(flatland, true);

  // Remove the entire hierarchy, then verify that the image is still present.
  flatland->SetRootTransform({0});
  PRESENT(flatland, true);

  auto uber_struct = GetUberStruct(flatland.get());
  auto image_kv = uber_struct->images.find(image_handle);
  EXPECT_NE(image_kv, uber_struct->images.end());
  EXPECT_EQ(image_kv->second.collection_id, global_collection_id1);

  // Reintroduce the hierarchy and confirm the Image is still present, even though it was
  // temporarily not reachable from the root transform.
  flatland->SetRootTransform(kTransformId);
  PRESENT(flatland, true);

  uber_struct = GetUberStruct(flatland.get());
  EXPECT_EQ(uber_struct->local_topology.back().handle, image_handle);

  image_kv = uber_struct->images.find(image_handle);
  EXPECT_NE(image_kv, uber_struct->images.end());
  EXPECT_EQ(image_kv->second.collection_id, global_collection_id1);
}

TEST_F(FlatlandTest, ClearReleasesImagesAndBufferCollections) {
  std::shared_ptr<Allocator> allocator = CreateAllocator();
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // Setup a valid image.
  const ContentId kImageId = {1};
  BufferCollectionImportExportTokens ref_pair_1 = BufferCollectionImportExportTokens::New();

  ImageProperties properties1;
  properties1.set_size({100, 200});

  auto import_token_dup = ref_pair_1.DuplicateImportToken();
  const allocation::GlobalBufferCollectionId global_collection_id1 =
      CreateImage(flatland.get(), allocator.get(), kImageId, std::move(ref_pair_1),
                  std::move(properties1))
          .collection_id;

  // Create a transform, make it the root transform, and attach the Image.
  const TransformId kTransformId = {2};

  flatland->CreateTransform(kTransformId);
  flatland->SetRootTransform(kTransformId);
  flatland->SetContent(kTransformId, kImageId);
  PRESENT(flatland, true);

  // Clear the graph, then signal the release fence and ensure the buffer collection is released.
  flatland->Clear();
  import_token_dup.value.reset();

  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(global_collection_id1, _))
      .Times(1);
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(1);
  PRESENT(flatland, true);

  // The Image ID should be available for re-use.
  BufferCollectionImportExportTokens ref_pair_2 = BufferCollectionImportExportTokens::New();
  ImageProperties properties2;
  properties2.set_size({400, 800});

  const allocation::GlobalBufferCollectionId global_collection_id2 =
      CreateImage(flatland.get(), allocator.get(), kImageId, std::move(ref_pair_2),
                  std::move(properties2))
          .collection_id;

  EXPECT_NE(global_collection_id1, global_collection_id2);

  // Verify that the Image is valid and can be attached to a transform.
  flatland->CreateTransform(kTransformId);
  flatland->SetRootTransform(kTransformId);
  flatland->SetContent(kTransformId, kImageId);
  PRESENT(flatland, true);
}

TEST_F(FlatlandTest, UnsquashableUpdates_ShouldBeReflectedInScheduleUpdates) {
  std::shared_ptr<Flatland> flatland = CreateFlatland();

  // We call Present() twice, each time passing a different value as the unsquashable argument.
  // We EXPECT that the ensuing ScheduleUpdateForSession() call to the frame scheduler will
  // reflect the passed in squashable value.

  // Present with the unsquashable field set to true.
  {
    PresentArgs args;
    args.unsquashable = true;
    PRESENT_WITH_ARGS(flatland, std::move(args), true);
  }

  // Present with the unsquashable field set to false.
  {
    PresentArgs args;
    args.unsquashable = false;
    PRESENT_WITH_ARGS(flatland, std::move(args), true);
  }
}

TEST_F(FlatlandTest, MultithreadedLinkResolution) {
  auto parent_flatland = CreateFlatland();

  std::shared_ptr<Flatland> child_flatland;
  fuchsia::ui::composition::FlatlandPtr child_flatland_ptr;
  auto child_flatland_thread_loop_holder =
      std::make_shared<utils::LoopDispatcherHolder>(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto& child_flatland_thread_loop = child_flatland_thread_loop_holder->loop();

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;

  {
    auto session_id = scheduling::GetNextSessionId();
    std::vector<std::shared_ptr<BufferCollectionImporter>> importers;
    importers.push_back(buffer_collection_importer_);

    child_flatland = Flatland::New(
        child_flatland_thread_loop_holder, child_flatland_ptr.NewRequest(), session_id,
        [](auto...) {}, flatland_presenter_, link_system_,
        uber_struct_system_->AllocateQueueForSession(session_id), importers, [](auto...) {},
        [](auto...) {}, [](auto...) {}, [](auto...) {});

    auto status = child_flatland_thread_loop.StartThread();
    EXPECT_EQ(status, ZX_OK);
  }

  auto creation_tokens = scenic::ViewCreationTokenPair::New();
  const TransformId kRootTransform = {1};
  const ContentId kLinkId = {1};

  // One of the link ends needs to run first.  Here, we arbitrarily choose it to be the viewport
  // end, and wait for it to finish.  Of course, the link is not yet resolved, because the view
  // hasn't yet been created.
  ViewportProperties properties;
  properties.set_logical_size({kDefaultSize, kDefaultSize});
  parent_flatland->CreateTransform(kRootTransform);
  parent_flatland->SetRootTransform(kRootTransform);
  parent_flatland->CreateViewport(kLinkId, std::move(creation_tokens.viewport_token),
                                  std::move(properties), child_view_watcher.NewRequest());
  parent_flatland->SetContent(kRootTransform, kLinkId);
  PRESENT(parent_flatland, true);
  RunLoopUntilIdle();

  ApplySessionUpdatesAndSignalFences();
  UpdateLinks(parent_flatland->GetRoot());
  auto links = link_system_->GetResolvedTopologyLinks();
  EXPECT_EQ(links.size(), 0U);

  // We post this task onto the other Flatland's thread, so that we can have a *chance* of locking
  // the LinkSystem mutex in between the execution of the two link-resolution closures (each of
  // which also locks the LinkSystem mutex).
  bool presented = false;
  EXPECT_CALL(*mock_flatland_presenter_, ScheduleUpdateForSession(_, _, _, _));
  async::PostTask(child_flatland_thread_loop.dispatcher(), ([&]() {
                    child_flatland->CreateView2(
                        std::move(creation_tokens.view_token), scenic::NewViewIdentityOnCreation(),
                        NoViewProtocols(), parent_viewport_watcher.NewRequest());

                    fuchsia::ui::composition::PresentArgs present_args;
                    present_args.set_requested_presentation_time(0);
                    present_args.set_acquire_fences({});
                    present_args.set_release_fences({});
                    present_args.set_unsquashable({});
                    child_flatland->Present(std::move(present_args));

                    // `Present()` puts the resulting UberStruct into the "acquire fence queue";
                    // since there are no fences it is not-quite-immediately made available via a
                    // posted task.  If we were to quit immediately, then this task wouldn't get a
                    // chance to run.  So instead, we wrap `Quit()` in a task that will run after
                    // UberStruct is made available.
                    async::PostTask(child_flatland_thread_loop.dispatcher(),
                                    [&]() { child_flatland_thread_loop.Quit(); });
                  }));

  // This runs "concurrently" with the task above.  Ideally, it will sometimes fall between the
  // running the two ObjectLinker link-resolved closures.  Unfortunately this is unlikely, so we
  // can't reliably ensure that this case is handled properly.  More often, this will occur either
  // before both link-resolved closures, or after both of them (both of these situations are handled
  // properly).
  ApplySessionUpdatesAndSignalFences();
  UpdateLinks(parent_flatland->GetRoot());
  links = link_system_->GetResolvedTopologyLinks();
  // One of these will be true, but we don't know which one.
  // EXPECT_EQ(links.size(), 0U);
  // EXPECT_EQ(links.size(), 1U);

  // By waiting for the task to finish running, we know that the Present() has happened, and
  // therefore both the parent and child Flatland sessions will have provided UberStructs, so
  // that there will now be a resolved link between the viewport and the view.
  child_flatland_thread_loop.JoinThreads();
  ApplySessionUpdatesAndSignalFences();
  UpdateLinks(parent_flatland->GetRoot());
  links = link_system_->GetResolvedTopologyLinks();
  EXPECT_EQ(links.size(), 1U);
}

// Verify that `destroy_instance_function_()` is only invoked once, even if there are multiple
// errors that each would alone cause `destroy_instance_function_()` to be invoked.
TEST_F(FlatlandTest, NoDoubleDestroyRequest) {
  // Create flatland and allocator instances that has two BufferCollectionImporters.
  std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> no_importers;
  std::shared_ptr<Allocator> allocator =
      std::make_shared<Allocator>(context_provider_.context(), no_importers, no_importers,
                                  utils::CreateSysmemAllocatorSyncPtr("NoDoubleDestroyRequest"));

  auto session_id = scheduling::GetNextSessionId();
  fuchsia::ui::composition::FlatlandPtr flatland_ptr;

  size_t destroy_instance_function_invocation_count = 0;

  auto flatland = Flatland::New(
      std::make_shared<utils::UnownedDispatcherHolder>(dispatcher()), flatland_ptr.NewRequest(),
      session_id,
      /*destroy_instance_functon=*/
      [&destroy_instance_function_invocation_count]() {
        ++destroy_instance_function_invocation_count;
      },
      flatland_presenter_, link_system_, uber_struct_system_->AllocateQueueForSession(session_id),
      no_importers, [](auto...) {}, [](auto...) {}, [](auto...) {}, [](auto...) {});

  fuchsia::ui::composition::PresentArgs present_args;
  present_args.set_requested_presentation_time(0);
  present_args.set_acquire_fences({});
  present_args.set_release_fences({});
  present_args.set_unsquashable({});

  flatland->AddChild(TransformId{.value = 11}, TransformId{.value = 12});
  flatland->Present(std::move(present_args));

  EXPECT_EQ(destroy_instance_function_invocation_count, 1U);

  flatland->AddChild(TransformId{.value = 11}, TransformId{.value = 12});
  flatland->Present(std::move(present_args));

  // If it wasn't for the guard variable `destroy_instance_function_was_invoked_` in
  // `Flatland::CloseConnection()`, this check would fail deterministically.
  EXPECT_EQ(destroy_instance_function_invocation_count, 1U);
}

TEST_F(FlatlandDisplayTest, SimpleSetContent) {
  constexpr uint32_t kWidth = 800;
  constexpr uint32_t kHeight = 600;

  std::shared_ptr<FlatlandDisplay> display = CreateFlatlandDisplay(kWidth, kHeight);
  std::shared_ptr<Flatland> child = CreateFlatland();

  const ContentId kLinkId1 = {1};

  fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;

  SetDisplayContent(display.get(), child.get(), &child_view_watcher, &parent_viewport_watcher);

  std::optional<LayoutInfo> layout_info;
  parent_viewport_watcher->GetLayout(
      [&](LayoutInfo new_info) { layout_info = std::move(new_info); });

  std::optional<ParentViewportStatus> parent_viewport_watcher_status;
  parent_viewport_watcher->GetStatus(
      [&](ParentViewportStatus new_status) { parent_viewport_watcher_status = new_status; });

  RunLoopUntilIdle();

  // LayoutInfo is sent as soon as the content/graph link is established, to allow clients to
  // generate their first frame with minimal latency.
  EXPECT_TRUE(layout_info.has_value());
  EXPECT_EQ(layout_info.value().logical_size().width, kWidth);
  EXPECT_EQ(layout_info.value().logical_size().height, kHeight);

  // The ParentViewportWatcher's status must wait until the first frame is generated (represented
  // here by the call to UpdateLinks() below).
  EXPECT_FALSE(parent_viewport_watcher_status.has_value());

  UpdateLinks(display->root_transform());

  // UpdateLinks() causes us to receive the status notification.  The link is considered to be
  // disconnected because the child has not yet presented its first frame.
  EXPECT_TRUE(parent_viewport_watcher_status.has_value());
  EXPECT_EQ(parent_viewport_watcher_status.value(),
            ParentViewportStatus::DISCONNECTED_FROM_DISPLAY);
  parent_viewport_watcher_status.reset();

  PRESENT(child, true);

  // The status won't change to "connected" until UpdateLinks() is called again.
  parent_viewport_watcher->GetStatus(
      [&](ParentViewportStatus new_status) { parent_viewport_watcher_status = new_status; });
  RunLoopUntilIdle();
  EXPECT_FALSE(parent_viewport_watcher_status.has_value());

  UpdateLinks(display->root_transform());

  EXPECT_TRUE(parent_viewport_watcher_status.has_value());
  EXPECT_EQ(parent_viewport_watcher_status.value(), ParentViewportStatus::CONNECTED_TO_DISPLAY);
}

// TODO(fxbug.dev/76640): other FlatlandDisplayTests that should be written:
// - version of SimpleSetContent where the child presents before SetDisplayContent() is called.
// - call SetDisplayContent() multiple times.

#undef EXPECT_MATRIX
#undef PRESENT
#undef PRESENT_WITH_ARGS
#undef REGISTER_BUFFER_COLLECTION

}  // namespace test
}  // namespace flatland
