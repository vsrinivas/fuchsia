// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_H_

#include <fuchsia/ui/scenic/internal/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>

#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// clang-format off
#include "src/ui/lib/glm_workaround/glm_workaround.h"
// clang-format on

#include <glm/vec2.hpp>
#include <glm/mat3x3.hpp>

#include "src/ui/scenic/lib/flatland/link_system.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"
#include "src/ui/scenic/lib/flatland/flatland_presenter.h"
#include "src/ui/scenic/lib/flatland/transform_graph.h"
#include "src/ui/scenic/lib/flatland/transform_handle.h"
#include "src/ui/scenic/lib/flatland/uber_struct_system.h"
#include "src/ui/lib/escher/flib/fence_queue.h"
#include "src/ui/scenic/lib/gfx/engine/object_linker.h"
#include "src/ui/scenic/lib/scheduling/id.h"

#include "src/ui/scenic/lib/flatland/buffer_collection_importer.h"

namespace flatland {

// This is a WIP implementation of the 2D Layer API. It currently exists to run unit tests, and to
// provide a platform for features to be iterated and implemented over time.
class Flatland : public fuchsia::ui::scenic::internal::Flatland {
 public:
  using TransformId = uint64_t;
  using BufferCollectionId = uint64_t;
  using ContentId = uint64_t;

  // Passing the same LinkSystem and UberStructSystem to multiple Flatland instances will allow
  // them to link to each other through operations that involve tokens and parent/child
  // relationships (e.g., by calling LinkToParent() and CreateLink()).
  explicit Flatland(
      scheduling::SessionId session_id,
      const std::shared_ptr<FlatlandPresenter>& flatland_presenter,
      const std::shared_ptr<Renderer>& renderer, const std::shared_ptr<LinkSystem>& link_system,
      const std::shared_ptr<UberStructSystem::UberStructQueue>& uber_struct_queue,
      const std::vector<std::shared_ptr<BufferCollectionImporter>>& buffer_collection_importers,
      fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator);
  ~Flatland();

  // Because this object captures its "this" pointer in internal closures, it is unsafe to copy or
  // move it. Disable all copy and move operations.
  Flatland(const Flatland&) = delete;
  Flatland& operator=(const Flatland&) = delete;
  Flatland(Flatland&&) = delete;
  Flatland& operator=(Flatland&&) = delete;

  // |fuchsia::ui::scenic::internal::Flatland|
  void Present(zx_time_t requested_presentation_time, std::vector<zx::event> acquire_fences,
               std::vector<zx::event> release_fences, PresentCallback callback) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void LinkToParent(
      fuchsia::ui::scenic::internal::GraphLinkToken token,
      fidl::InterfaceRequest<fuchsia::ui::scenic::internal::GraphLink> graph_link) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void UnlinkFromParent(
      fuchsia::ui::scenic::internal::Flatland::UnlinkFromParentCallback callback) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void ClearGraph() override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void CreateTransform(TransformId transform_id) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void SetTranslation(TransformId transform_id,
                      fuchsia::ui::scenic::internal::Vec2 translation) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void SetOrientation(TransformId transform_id,
                      fuchsia::ui::scenic::internal::Orientation orientation) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void SetScale(TransformId transform_id, fuchsia::ui::scenic::internal::Vec2 scale) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void AddChild(TransformId parent_transform_id, TransformId child_transform_id) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void RemoveChild(TransformId parent_transform_id, TransformId child_transform_id) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void SetRootTransform(TransformId transform_id) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void CreateLink(
      ContentId link_id, fuchsia::ui::scenic::internal::ContentLinkToken token,
      fuchsia::ui::scenic::internal::LinkProperties properties,
      fidl::InterfaceRequest<fuchsia::ui::scenic::internal::ContentLink> content_link) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void RegisterBufferCollection(
      BufferCollectionId collection_id,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void CreateImage(ContentId image_id, BufferCollectionId collection_id, uint32_t vmo_index,
                   fuchsia::ui::scenic::internal::ImageProperties properties) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void SetContentOnTransform(ContentId content_id, TransformId transform_id) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void SetLinkProperties(ContentId link_id,
                         fuchsia::ui::scenic::internal::LinkProperties properties) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void SetLinkSize(ContentId link_id, fuchsia::ui::scenic::internal::Vec2 size) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void ReleaseTransform(TransformId transform_id) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void ReleaseLink(ContentId link_id,
                   fuchsia::ui::scenic::internal::Flatland::ReleaseLinkCallback callback) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void DeregisterBufferCollection(BufferCollectionId collection_id) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void ReleaseImage(ContentId image_id) override;

  // For validating the transform hierarchy in tests only. For the sake of testing, the "root" will
  // always be the top-most TransformHandle from the TransformGraph owned by this Flatland. If
  // currently linked to a parent, that means the link_origin. If not, that means the local_root_.
  TransformHandle GetRoot() const;

  // For validating properties associated with content in tests only. If |content_id| does not
  // exist for this Flatland instance, returns std::nullopt.
  std::optional<TransformHandle> GetContentHandle(ContentId content_id) const;

 private:
  void ReportError();

  // Users are not allowed to use zero as a transform ID.
  static constexpr TransformId kInvalidId = 0;

  // This is the maximum number of pending Present() calls the user can have in flight. Since the
  // current implementation is synchronous, there can only be one call to Present() at a time.
  //
  // TODO(fxbug.dev/36161): Tune this number once we have a non-synchronous present flow.
  static constexpr uint32_t kMaxPresents = 1;

  // The unique SessionId for this Flatland instance. Used to schedule Presents and register
  // UberStructs with the UberStructSystem.
  const scheduling::SessionId session_id_;

  // A FlatlandPresenter shared between Flatland instances. Flatland uses this interface to get
  // PresentIds when publishing to the UberStructSystem.
  std::shared_ptr<FlatlandPresenter> flatland_presenter_;

  // A Renderer shared between Flatland instances. Flatland registers buffer collections with the
  // Renderer and references them by ID when submitting data in an UberStruct.
  std::shared_ptr<Renderer> renderer_;

  // A link system shared between Flatland instances, so that links can be made between them.
  std::shared_ptr<LinkSystem> link_system_;

  // An UberStructSystem shared between Flatland instances. Flatland publishes local data to the
  // UberStructSystem in order to have it seen by the global render loop.
  std::shared_ptr<UberStructSystem::UberStructQueue> uber_struct_queue_;

  // Used to import Flatland buffer collections and images to external services that Flatland does
  // not have knowledge of. Each importer is used for a different service.
  std::vector<std::shared_ptr<BufferCollectionImporter>> buffer_collection_importers_;

  // A Sysmem allocator to faciliate buffer allocation with the Renderer.
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;

  // True if any function has failed since the previous call to Present(), false otherwise.
  bool failure_since_previous_present_ = false;

  // The number of pipelined Present() operations available to the client.
  uint32_t num_presents_remaining_ = kMaxPresents;

  // Must be managed by a shared_ptr because the implementation uses weak_from_this().
  std::shared_ptr<escher::FenceQueue> fence_queue_ = std::make_shared<escher::FenceQueue>();

  // A map from user-generated ID to global handle. This map constitutes the set of transforms that
  // can be referenced by the user through method calls. Keep in mind that additional transforms may
  // be kept alive through child references.
  std::unordered_map<TransformId, TransformHandle> transforms_;

  // A graph representing this flatland instance's local transforms and their relationships.
  TransformGraph transform_graph_;

  // A unique transform for this instance, the local_root_, is part of the transform_graph_,
  // and will never be released or changed during the course of the instance's lifetime. This makes
  // it a fixed attachment point for cross-instance Links.
  const TransformHandle local_root_;

  // A mapping from user-generated ID to the TransformHandle that owns that piece of Content.
  // Attaching Content to a Transform consists of setting one of these "Content Handles" as the
  // priority child of the Transform.
  std::unordered_map<ContentId, TransformHandle> content_handles_;

  // The set of link operations that are pending a call to Present(). Unlike other operations,
  // whose effects are only visible when a new UberStruct is published, Link destruction operations
  // result in immediate changes in the LinkSystem. To avoid having these changes visible before
  // Present() is called, the actual destruction of Links happens in the following Present().
  std::vector<fit::function<void()>> pending_link_operations_;

  // Wraps a LinkSystem::ChildLink and the properties currently associated with that link.
  struct ChildLinkData {
    LinkSystem::ChildLink link;
    fuchsia::ui::scenic::internal::LinkProperties properties;
    fuchsia::ui::scenic::internal::Vec2 size;
  };

  // Recomputes the scale matrix responsible for fitting a Link's logical size into the actual size
  // designated for it.
  void UpdateLinkScale(const ChildLinkData& link_data);

  // A mapping from Flatland-generated TransformHandle to the ChildLinkData it represents.
  std::unordered_map<TransformHandle, ChildLinkData> child_links_;

  // The link from this Flatland instance to our parent.
  std::optional<LinkSystem::ParentLink> parent_link_;

  // Represents a geometric transformation as three separate components applied in the following
  // order: translation (relative to the parent's coordinate space), orientation (around the new
  // origin as defined by the translation), and scale (relative to the new rotated origin).
  class MatrixData {
   public:
    void SetTranslation(fuchsia::ui::scenic::internal::Vec2 translation);
    void SetOrientation(fuchsia::ui::scenic::internal::Orientation orientation);
    void SetScale(fuchsia::ui::scenic::internal::Vec2 scale);

    // Returns this geometric transformation as a single 3x3 matrix using the order of operations
    // above: translation, orientation, then scale.
    glm::mat3 GetMatrix() const;

    static float GetOrientationAngle(fuchsia::ui::scenic::internal::Orientation orientation);

   private:
    // Applies the translation, then orientation, then scale to the identity matrix.
    void RecomputeMatrix();

    glm::vec2 translation_ = glm::vec2(0.f, 0.f);
    glm::vec2 scale_ = glm::vec2(1.f, 1.f);

    // Counterclockwise rotation angle, in radians.
    float angle_ = 0.f;

    // Recompute and cache the local matrix each time a component is changed to avoid recomputing
    // the matrix for each frame. We expect GetMatrix() to be called far more frequently (roughly
    // once per rendered frame) than the setters are called.
    glm::mat3 matrix_ = glm::mat3(1.f);
  };

  // A geometric transform for each TransformHandle. If not present, that TransformHandle has the
  // identity matrix for its transform.
  std::unordered_map<TransformHandle, MatrixData> matrices_;

  // A mapping from user-generated buffer collection IDs to global buffer collection
  // IDs.
  std::unordered_map<BufferCollectionId, sysmem_util::GlobalBufferCollectionId>
      buffer_collection_ids_;

  // The metadata associated with a particular buffer collection and the number of Images that
  // currently reference that buffer collection.
  struct BufferCollectionData {
    std::optional<BufferCollectionMetadata> metadata;
    size_t image_count = 0;
  };

  // A mapping from global buffer collection ID to the data associated with each
  // collection.
  std::unordered_map<sysmem_util::GlobalBufferCollectionId, BufferCollectionData>
      buffer_collections_;

  // The set of sysmem_util::GlobalBufferCollectionIds associated with released BufferCollectionIds
  // that have not yet been garbage collected.
  std::unordered_set<sysmem_util::GlobalBufferCollectionId> released_buffer_collection_ids_;

  // A mapping from Flatland-generated TransformHandle to the ImageMetadata it represents.
  std::unordered_map<TransformHandle, ImageMetadata> image_metadatas_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_H_
