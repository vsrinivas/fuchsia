// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_H_

#include <fuchsia/ui/scenic/internal/cpp/fidl.h>
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
#include "src/ui/scenic/lib/flatland/transform_graph.h"
#include "src/ui/scenic/lib/flatland/transform_handle.h"
#include "src/ui/scenic/lib/flatland/uber_struct_system.h"
#include "src/ui/scenic/lib/gfx/engine/object_linker.h"

namespace flatland {

// This is a WIP implementation of the 2D Layer API. It currently exists to run unit tests, and to
// provide a platform for features to be iterated and implemented over time.
class Flatland : public fuchsia::ui::scenic::internal::Flatland {
 public:
  using TransformId = uint64_t;
  using LinkId = uint64_t;

  // Passing the same LinkSystem and TopologySystem to multiple Flatland instances will allow them
  // to link to each other through operations that involve tokens and parent/child relationships
  // (e.g., by calling LinkToParent() and CreateLink()).
  explicit Flatland(const std::shared_ptr<LinkSystem>& link_system,
                    const std::shared_ptr<UberStructSystem>& uber_struct_system);
  ~Flatland();

  // Because this object captures its "this" pointer in internal closures, it is unsafe to copy or
  // move it. Disable all copy and move operations.
  Flatland(const Flatland&) = delete;
  Flatland& operator=(const Flatland&) = delete;
  Flatland(Flatland&&) = delete;
  Flatland& operator=(Flatland&&) = delete;

  // |fuchsia::ui::scenic::internal::Flatland|
  void Present(PresentCallback callback) override;
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
      LinkId link_id, fuchsia::ui::scenic::internal::ContentLinkToken token,
      fuchsia::ui::scenic::internal::LinkProperties properties,
      fidl::InterfaceRequest<fuchsia::ui::scenic::internal::ContentLink> content_link) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void SetLinkOnTransform(LinkId link_id, TransformId transform_id) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void SetLinkProperties(LinkId id,
                         fuchsia::ui::scenic::internal::LinkProperties properties) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void ReleaseTransform(TransformId transform_id) override;
  // |fuchsia::ui::scenic::internal::Flatland|
  void ReleaseLink(LinkId link_id,
                   fuchsia::ui::scenic::internal::Flatland::ReleaseLinkCallback callback) override;

  // For validating the transform hierarchy in tests only. For the sake of testing, the "root" will
  // always be the top-most TransformHandle from the TransformGraph owned by this Flatland. If
  // currently linked to a parent, that means the link_origin. If not, that means the local_root_.
  TransformHandle GetRoot() const;

 private:
  // Users are not allowed to use zero as a transform ID.
  static constexpr TransformId kInvalidId = 0;

  // This is the maximum number of pending Present() calls the user can have in flight. Since the
  // current implementation is synchronous, there can only be one call to Present() at a time.
  //
  // TODO(36161): Tune this number once we have a non-synchronous present flow.
  static constexpr uint32_t kMaxPresents = 1;

  using TransformMap = std::map<TransformId, TransformHandle>;

  // A link system shared between Flatland instances, so that links can be made between them.
  std::shared_ptr<LinkSystem> link_system_;

  // An UberStructSystem shared between Flatland instances. Flatland publishes local data to the
  // UberStructSystem in order to have it seen by the global render loop.
  std::shared_ptr<UberStructSystem> uber_struct_system_;

  // The set of operations that are pending a call to Present().
  std::vector<fit::function<bool()>> pending_operations_;

  // The number of pipelined Present() operations available to the client.
  uint32_t num_presents_remaining_ = kMaxPresents;

  // A map from user-generated id to global handle. This map constitutes the set of transforms that
  // can be referenced by the user through method calls. Keep in mind that additional transforms may
  // be kept alive through child references.
  TransformMap transforms_;

  // A unique ID from the UberStructSystem representing this Flatland instance.
  const TransformHandle::InstanceId instance_id_;

  // A graph representing this flatland instance's local transforms and their relationships.
  TransformGraph transform_graph_;

  // A unique transform for this instance, the local_root_, is part of the transform_graph_,
  // and will never be released or changed during the course of the instance's lifetime. This makes
  // it a fixed attachment point for cross-instance Links.
  const TransformHandle local_root_;

  // Wraps a LinkSystem::ChildLink and the LinkProperties currently associated with that link.
  struct ChildLinkData {
    LinkSystem::ChildLink link;
    fuchsia::ui::scenic::internal::LinkProperties properties;
  };

  // A mapping from user-generated id to ChildLinkData.
  std::unordered_map<LinkId, ChildLinkData> child_links_;

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
    float angle_ = 0.f;

    // Recompute and cache the local matrix each time a component is changed to avoid recomputing
    // the matrix for each frame. We expect GetMatrix() to be called far more frequently (roughly
    // once per rendered frame) than the setters are called.
    glm::mat3 matrix_ = glm::mat3(1.f);
  };

  // A geometric transform for each TransformHandle. If not present, that TransformHandle has the
  // identity matrix for its transform.
  std::unordered_map<TransformHandle, MatrixData> matrices_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_H_
