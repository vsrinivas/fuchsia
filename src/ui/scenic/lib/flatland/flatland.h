// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_H_

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/ui/lib/escher/flib/fence_queue.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/flatland/flatland_presenter.h"
#include "src/ui/scenic/lib/flatland/link_system.h"
#include "src/ui/scenic/lib/flatland/transform_graph.h"
#include "src/ui/scenic/lib/flatland/transform_handle.h"
#include "src/ui/scenic/lib/flatland/uber_struct_system.h"
#include "src/ui/scenic/lib/gfx/engine/object_linker.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"
#include "src/ui/scenic/lib/scheduling/id.h"
#include "src/ui/scenic/lib/scheduling/present2_helper.h"
#include "src/ui/scenic/lib/utils/dispatcher_holder.h"

#include <glm/glm.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec2.hpp>

namespace flatland {

// This is a WIP implementation of the 2D Layer API. It currently exists to run unit tests, and to
// provide a platform for features to be iterated and implemented over time.
// All methods following constructor run on |dispatcher_|.
class Flatland : public fuchsia::ui::composition::Flatland,
                 public std::enable_shared_from_this<Flatland> {
 public:
  using BufferCollectionId = uint64_t;
  using ContentId = fuchsia::ui::composition::ContentId;
  using FuturePresentationInfos = std::vector<fuchsia::scenic::scheduling::PresentationInfo>;
  using TransformId = fuchsia::ui::composition::TransformId;

  // Binds this Flatland object to serve |request| on |dispatcher|. The |destroy_instance_function|
  // will be invoked from the Looper that owns |dispatcher| when this object is ready to be cleaned
  // up (e.g. when the client closes their side of the channel or encounters makes an unrecoverable
  // API call error).
  //
  // |flatland_presenter|, |link_system|, |uber_struct_queue|, and |buffer_collection_importers|
  // allow this Flatland object to access resources shared by all Flatland instances for actions
  // like frame scheduling, linking, buffer allocation, and presentation to the global scene graph.
  static std::shared_ptr<Flatland> New(
      std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
      fidl::InterfaceRequest<fuchsia::ui::composition::Flatland> request,
      scheduling::SessionId session_id, std::function<void()> destroy_instance_function,
      std::shared_ptr<FlatlandPresenter> flatland_presenter,
      std::shared_ptr<LinkSystem> link_system,
      std::shared_ptr<UberStructSystem::UberStructQueue> uber_struct_queue,
      const std::vector<std::shared_ptr<allocation::BufferCollectionImporter>>&
          buffer_collection_importers,
      fit::function<void(fidl::InterfaceRequest<fuchsia::ui::views::Focuser>, zx_koid_t)>
          register_view_focuser,
      fit::function<void(fidl::InterfaceRequest<fuchsia::ui::views::ViewRefFocused>, zx_koid_t)>
          register_view_ref_focused,
      fit::function<void(fidl::InterfaceRequest<fuchsia::ui::pointer::TouchSource>, zx_koid_t)>
          register_touch_source,
      fit::function<void(fidl::InterfaceRequest<fuchsia::ui::pointer::MouseSource>, zx_koid_t)>
          register_mouse_source);
  ~Flatland();

  // Because this object captures its "this" pointer in internal closures, it is unsafe to copy or
  // move it. Disable all copy and move operations.
  Flatland(const Flatland&) = delete;
  Flatland& operator=(const Flatland&) = delete;
  Flatland(Flatland&&) = delete;
  Flatland& operator=(Flatland&&) = delete;

  // |fuchsia::ui::composition::Flatland|
  void Present(fuchsia::ui::composition::PresentArgs args) override;
  // |fuchsia::ui::composition::Flatland|
  void CreateView(fuchsia::ui::views::ViewCreationToken token,
                  fidl::InterfaceRequest<fuchsia::ui::composition::ParentViewportWatcher>
                      parent_viewport_watcher) override;
  void CreateView2(fuchsia::ui::views::ViewCreationToken token,
                   fuchsia::ui::views::ViewIdentityOnCreation view_identity,
                   fuchsia::ui::composition::ViewBoundProtocols protocols,
                   fidl::InterfaceRequest<fuchsia::ui::composition::ParentViewportWatcher>
                       parent_viewport_watcher) override;
  // |fuchsia::ui::composition::Flatland|
  // TODO(fxbug.dev/81576): Consider returning tokens for re-linking.
  void ReleaseView() override;
  // |fuchsia::ui::composition::Flatland|
  void Clear() override;
  // |fuchsia::ui::composition::Flatland|
  void CreateTransform(TransformId transform_id) override;
  // |fuchsia::ui::composition::Flatland|
  void SetTranslation(TransformId transform_id, fuchsia::math::Vec translation) override;
  // |fuchsia::ui::composition::Flatland|
  void SetOrientation(TransformId transform_id,
                      fuchsia::ui::composition::Orientation orientation) override;
  // |fuchsia::ui::composition::Flatland|
  void SetScale(TransformId transform_id, fuchsia::math::VecF scale) override;
  // |fuchsia::ui::composition::Flatland|
  void SetOpacity(TransformId transform_id, float value) override;
  // |fuchsia::ui::composition::Flatland|
  void SetClipBoundary(TransformId transform_id,
                       std::unique_ptr<fuchsia::math::Rect> bounds) override;
  // |fuchsia::ui::composition::Flatland|
  void AddChild(TransformId parent_transform_id, TransformId child_transform_id) override;
  // |fuchsia::ui::composition::Flatland|
  void RemoveChild(TransformId parent_transform_id, TransformId child_transform_id) override;
  // |fuchsia::ui::composition::Flatland|
  void SetRootTransform(TransformId transform_id) override;
  // |fuchsia::ui::composition::Flatland|
  void CreateViewport(ContentId viewport_id, fuchsia::ui::views::ViewportCreationToken token,
                      fuchsia::ui::composition::ViewportProperties properties,
                      fidl::InterfaceRequest<fuchsia::ui::composition::ChildViewWatcher>
                          child_view_watcher) override;
  // |fuchsia::ui::composition::Flatland|
  void CreateImage(ContentId image_id,
                   fuchsia::ui::composition::BufferCollectionImportToken import_token,
                   uint32_t vmo_index,
                   fuchsia::ui::composition::ImageProperties properties) override;
  // |fuchsia::ui::composition::Flatland|
  void SetImageSampleRegion(ContentId image_id, fuchsia::math::RectF rect) override;
  // |fuchsia::ui::composition::Flatland|
  void SetImageDestinationSize(ContentId image_id, fuchsia::math::SizeU size) override;
  // |fuchsia::ui::composition::Flatland|
  void SetImageBlendingFunction(ContentId image_id,
                                fuchsia::ui::composition::BlendMode blend_mode) override;
  // |fuchsia::ui::composition::Flatland|
  void SetImageFlip(ContentId image_id, fuchsia::ui::composition::ImageFlip flip) override;
  // |fuchsia::ui::composition::Flatland|
  void CreateFilledRect(ContentId rect_id) override;
  // |fuchsia::ui::composition::Flatland|
  void SetSolidFill(ContentId rect_id, fuchsia::ui::composition::ColorRgba color,
                    fuchsia::math::SizeU size) override;
  // |fuchsia::ui::composition::Flatland|
  void ReleaseFilledRect(ContentId rect_id) override;
  // |fuchsia::ui::composition::Flatland|
  void SetImageOpacity(ContentId image_id, float val) override;
  // |fuchsia::ui::composition::Flatland|
  void SetHitRegions(TransformId transform_id,
                     std::vector<fuchsia::ui::composition::HitRegion> regions) override;
  // |fuchsia::ui::composition::Flatland|
  void SetContent(TransformId transform_id, ContentId content_id) override;
  // |fuchsia::ui::composition::Flatland|
  void SetViewportProperties(ContentId viewport_id,
                             fuchsia::ui::composition::ViewportProperties properties) override;
  // |fuchsia::ui::composition::Flatland|
  void ReleaseTransform(TransformId transform_id) override;
  // |fuchsia::ui::composition::Flatland|
  void ReleaseViewport(
      ContentId viewport_id,
      fuchsia::ui::composition::Flatland::ReleaseViewportCallback callback) override;
  // |fuchsia::ui::composition::Flatland|
  void ReleaseImage(ContentId image_id) override;
  // |fuchsia::ui::composition::Flatland|
  void SetDebugName(std::string name) override;

  // Called just before the FIDL client receives the event of the same name, indicating that this
  // Flatland instance should allow a |additional_present_credits| calls to Present().
  void OnNextFrameBegin(uint32_t additional_present_credits,
                        FuturePresentationInfos presentation_infos);

  // Called when this Flatland instance should send the OnFramePresented() event to the FIDL
  // client.
  void OnFramePresented(const std::map<scheduling::PresentId, zx::time>& latched_times,
                        scheduling::PresentTimestamps present_times);

  // For validating the transform hierarchy in tests only. For the sake of testing, the "root" will
  // always be the top-most TransformHandle from the TransformGraph owned by this Flatland. If
  // currently linked to a parent, that means the Link's child_transform_handle. If not, that means
  // the local_root_.
  TransformHandle GetRoot() const;

  // For validating properties associated with content in tests only. If |content_id| does not
  // exist for this Flatland instance, returns std::nullopt.
  std::optional<TransformHandle> GetContentHandle(ContentId content_id) const;

  // For validating properties associated with transforms in tests only. If |transform_id| does not
  // exist for this Flatland instance, returns std::nullopt.
  std::optional<TransformHandle> GetTransformHandle(TransformId transform_id) const;

  // For validating logs in tests only.
  void SetErrorReporter(std::unique_ptr<scenic_impl::ErrorReporter> error_reporter);

  // For using as a unique identifier in tests only.
  scheduling::SessionId GetSessionId() const;

 private:
  Flatland(
      std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
      fidl::InterfaceRequest<fuchsia::ui::composition::Flatland> request,
      scheduling::SessionId session_id, std::function<void()> destroy_instance_function,
      std::shared_ptr<FlatlandPresenter> flatland_presenter,
      std::shared_ptr<LinkSystem> link_system,
      std::shared_ptr<UberStructSystem::UberStructQueue> uber_struct_queue,
      const std::vector<std::shared_ptr<allocation::BufferCollectionImporter>>&
          buffer_collection_importers,
      fit::function<void(fidl::InterfaceRequest<fuchsia::ui::views::Focuser>, zx_koid_t)>
          register_view_focuser,
      fit::function<void(fidl::InterfaceRequest<fuchsia::ui::views::ViewRefFocused>, zx_koid_t)>
          register_view_ref_focused,
      fit::function<void(fidl::InterfaceRequest<fuchsia::ui::pointer::TouchSource>, zx_koid_t)>
          register_touch_source,
      fit::function<void(fidl::InterfaceRequest<fuchsia::ui::pointer::MouseSource>, zx_koid_t)>
          register_mouse_source);

  void ReportBadOperationError();
  void ReportLinkProtocolError(const std::string& error_log);
  void CloseConnection(fuchsia::ui::composition::FlatlandError error);

  // Note: Any new CreateView function must use this helper function for it to have the same
  // test coverage as its siblings.
  void CreateViewHelper(fuchsia::ui::views::ViewCreationToken token,
                        fidl::InterfaceRequest<fuchsia::ui::composition::ParentViewportWatcher>
                            parent_viewport_watcher,
                        std::optional<fuchsia::ui::views::ViewIdentityOnCreation> view_identity,
                        std::optional<fuchsia::ui::composition::ViewBoundProtocols> protocols);

  void RegisterViewBoundProtocols(fuchsia::ui::composition::ViewBoundProtocols protocols,
                                  zx_koid_t view_ref_koid);

  // Sets clip bounds on the provided transform handle. Takes in TransformHandle and not
  // TransformID as a parameter so that it can be applied to content transforms that do
  // not have an external ID that they are mapped to.
  void SetClipBoundaryInternal(TransformHandle handle, fuchsia::math::Rect bounds);

  // The dispatcher this Flatland instance is running on.
  async_dispatcher_t* dispatcher() const { return dispatcher_holder_->dispatcher(); }
  std::shared_ptr<utils::DispatcherHolder> dispatcher_holder_;

  // The FIDL binding for this Flatland instance, which references |this| as the implementation and
  // run on |dispatcher_|.
  fidl::Binding<fuchsia::ui::composition::Flatland> binding_;

  // Users are not allowed to use zero as a TransformId or ContentId.
  static constexpr uint64_t kInvalidId = 0;

  // The unique SessionId for this Flatland session. Used to schedule Presents and register
  // UberStructs with the UberStructSystem.
  const scheduling::SessionId session_id_;

  // A function that, when called, will destroy this instance. Necessary because an async::Wait can
  // only wait on peer channel destruction, not "this" channel destruction, so the FlatlandManager
  // cannot detect if this instance closes |binding_|.
  std::function<void()> destroy_instance_function_;

  // Keep track of whether |destroy_instance_function_| has been invoked, in order to guarantee that
  // it is called at most once.
  bool destroy_instance_function_was_invoked_ = false;

  // Waits for the invalidation of the bound channel, then triggers the destruction of this client.
  // Uses WaitOnce since calling the handler will result in the destruction of this object.
  async::WaitOnce peer_closed_waiter_;

  // A Present2Helper to facilitate sendng the appropriate OnFramePresented() callback to FIDL
  // clients when frames are presented to the display.
  scheduling::Present2Helper present2_helper_;

  // A FlatlandPresenter shared between Flatland instances. Flatland uses this interface to get
  // PresentIds when publishing to the UberStructSystem.
  std::shared_ptr<FlatlandPresenter> flatland_presenter_;

  // A link system shared between Flatland instances, so that links can be made between them.
  std::shared_ptr<LinkSystem> link_system_;

  // An UberStructSystem shared between Flatland instances. Flatland publishes local data to the
  // UberStructSystem in order to have it seen by the global render loop.
  std::shared_ptr<UberStructSystem::UberStructQueue> uber_struct_queue_;

  // Used to import Flatland images to external services that Flatland does not have knowledge of.
  // Each importer is used for a different service.
  std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> buffer_collection_importers_;

  // True if any function has failed since the previous call to Present(), false otherwise.
  bool failure_since_previous_present_ = false;

  // True if there was errors in ParentViewportWatcher or ChildViewWatcher channel provided.
  bool link_protocol_error_ = false;

  // The number of Present() calls remaining before the client runs out. This value is potentially
  // incremented when OnNextFrameBegin() is called, and decremented by 1 for each Present() call.
  uint32_t present_credits_ = 1;

  // Used for client->Flatland present flow IDs.
  uint64_t present_count_ = 0;

  // Must be managed by a shared_ptr because the implementation uses weak_from_this().
  std::shared_ptr<escher::FenceQueue> fence_queue_ = std::make_shared<escher::FenceQueue>();

  // A map from user-generated ID to global handle. This map constitutes the set of transforms that
  // can be referenced by the user through method calls. Keep in mind that additional transforms may
  // be kept alive through child references.
  std::unordered_map<uint64_t, TransformHandle> transforms_;

  // A graph representing this flatland instance's local transforms and their relationships.
  TransformGraph transform_graph_;

  // A unique transform for this instance, the local_root_, is part of the transform_graph_,
  // and will never be released or changed during the course of the instance's lifetime. This makes
  // it a fixed attachment point for cross-instance Links.
  const TransformHandle local_root_;

  // The transform from the last call to SetRootTransform(). Unlike |local_root_|, this can change
  // over time.
  //
  // Initialize to an invalid handle.
  TransformHandle root_transform_ = TransformHandle(0, 0);

  // A mapping from user-generated ID to the TransformHandle that owns that piece of Content.
  // Attaching Content to a Transform consists of setting one of these "Content Handles" as the
  // priority child of the Transform.
  std::unordered_map<uint64_t, TransformHandle> content_handles_;

  // The set of link operations that are pending a call to Present(). Unlike other operations,
  // whose effects are only visible when a new UberStruct is published, Link destruction operations
  // result in immediate changes in the LinkSystem. To avoid having these changes visible before
  // Present() is called, the actual destruction of Links happens in the following Present().
  std::vector<fit::function<void()>> pending_link_operations_;

  // Wraps a LinkSystem::LinkToChild and the properties currently associated with that link.
  struct LinkToChildData {
    LinkSystem::LinkToChild link;
    fuchsia::ui::composition::ViewportProperties properties;
    fuchsia::math::SizeU size;
  };

  // A mapping from Flatland-generated TransformHandle to the LinkToChildData it represents.
  std::unordered_map<TransformHandle, LinkToChildData> links_to_children_;

  // The link from this Flatland instance to our parent.
  std::optional<LinkSystem::LinkToParent> link_to_parent_;

  // Instance name from SetDebugName().
  std::string debug_name_;

  // Represents a geometric transformation as three separate components applied in the following
  // order: translation (relative to the parent's coordinate space), orientation (around the new
  // origin as defined by the translation), and scale (relative to the new rotated origin).
  class MatrixData {
   public:
    void SetTranslation(fuchsia::math::Vec translation);
    void SetOrientation(fuchsia::ui::composition::Orientation orientation);
    void SetScale(fuchsia::math::VecF scale);

    // Returns this geometric transformation as a single 3x3 matrix using the order of operations
    // above: translation, orientation, then scale.
    glm::mat3 GetMatrix() const;

    static float GetOrientationAngle(fuchsia::ui::composition::Orientation orientation);

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

  // A map of transform handles to opacity values where the values are strictly in the range
  // [0.f,1.f). 0.f is completely transparent and 1.f is completely opaque. If there is no explicit
  // value associated with a transform handle in this map, then Flatland will consider it to be
  // 1.f by default.
  std::unordered_map<TransformHandle, float> opacity_values_;

  // A map of transform handles to clip regions, where each clip region is a rect to which
  // all child nodes of the transform handle have their rectangular views clipped to.
  std::unordered_map<TransformHandle, TransformClipRegion> clip_regions_;

  // A map of transform handles to hit regions. Each transform's set of hit regions indicate which
  // parts of the transform are user-interactive.
  std::unordered_map<TransformHandle, std::vector<fuchsia::ui::composition::HitRegion>>
      hit_regions_;

  // A map of content (image) transform handles to ImageSampleRegion structs which are used
  // to determine the portion of an image that is actually used for rendering.
  std::unordered_map<TransformHandle, ImageSampleRegion> image_sample_regions_;

  // A mapping from Flatland-generated TransformHandle to the ImageMetadata it represents.
  std::unordered_map<TransformHandle, allocation::ImageMetadata> image_metadatas_;

  // Error reporter used for printing debug logs.
  std::unique_ptr<scenic_impl::ErrorReporter> error_reporter_;

  // Callbacks for registering View-bound protocols.
  fit::function<void(fidl::InterfaceRequest<fuchsia::ui::views::Focuser>, zx_koid_t)>
      register_view_focuser_;
  fit::function<void(fidl::InterfaceRequest<fuchsia::ui::views::ViewRefFocused>, zx_koid_t)>
      register_view_ref_focused_;
  fit::function<void(fidl::InterfaceRequest<fuchsia::ui::pointer::TouchSource>, zx_koid_t)>
      register_touch_source_;
  fit::function<void(fidl::InterfaceRequest<fuchsia::ui::pointer::MouseSource>, zx_koid_t)>
      register_mouse_source_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_H_
