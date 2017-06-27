// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "apps/mozart/services/scene/session.fidl.h"
#include "apps/mozart/src/scene/resources/memory.h"
#include "apps/mozart/src/scene/resources/resource_map.h"
#include "apps/mozart/src/scene/session/session_context.h"
#include "apps/mozart/src/scene/util/error_reporter.h"
#include "lib/ftl/tasks/task_runner.h"

namespace mozart {
namespace scene {

using SessionId = uint64_t;

class Image;
using ImagePtr = ::ftl::RefPtr<Image>;

class ImageBase;
using ImageBasePtr = ::ftl::RefPtr<ImageBase>;

class Session;
using SessionPtr = ::ftl::RefPtr<Session>;

// TODO: use unsafe ref-counting for better performance (our architecture
// guarantees that this is safe).
class Session : public ftl::RefCountedThreadSafe<Session> {
 public:
  Session(SessionId id,
          SessionContext* context,
          ErrorReporter* error_reporter = ErrorReporter::Default());
  ~Session();

  // Apply the operation to the current session state.  Return true if
  // successful, and false if the op is somehow invalid.  In the latter case,
  // the Session is left unchanged.
  bool ApplyOp(const mozart2::OpPtr& op);

  SessionId id() const { return id_; }
  SessionContext* context() const { return context_; }
  escher::Escher* escher() const { return context_->escher(); }

  // Return the total number of existing resources associated with this Session.
  size_t GetTotalResourceCount() const { return resource_count_; }

  // Return the number of resources that a client can identify via a ResourceId.
  // This number is decremented when a ReleaseResourceOp is applied.  However,
  // the resource may continue to exist if it is referenced by other resources.
  size_t GetMappedResourceCount() const { return resources_.size(); }

  void TearDown();

  // Session becomes invalid once TearDown is called.
  bool is_valid() const { return is_valid_; }

  ErrorReporter* error_reporter() const;

  ResourceMap* resources() { return &resources_; }

  // Called by SessionHandler::Present().  Stashes the arguments without
  // applying them; they will later be applied by ApplyScheduledUpdates().
  // TODO: nothing is currently done with the acquire and release fences.
  void ScheduleUpdate(uint64_t presentation_time,
                      ::fidl::Array<mozart2::OpPtr> ops,
                      ::fidl::Array<mx::event> wait_events,
                      ::fidl::Array<mx::event> signal_events,
                      const mozart2::Session::PresentCallback& callback);

  // Called by SessionContext() when it is notified by the FrameScheduler that
  // a frame should be rendered for the specified |presentation_time|.  Return
  // true if any updates were applied, and false otherwise.
  bool ApplyScheduledUpdates(uint64_t presentation_time,
                             uint64_t presentation_interval);

 private:
  // Operation application functions, called by ApplyOp().
  bool ApplyCreateResourceOp(const mozart2::CreateResourceOpPtr& op);
  bool ApplyReleaseResourceOp(const mozart2::ReleaseResourceOpPtr& op);
  bool ApplyExportResourceOp(const mozart2::ExportResourceOpPtr& op);
  bool ApplyImportResourceOp(const mozart2::ImportResourceOpPtr& op);
  bool ApplyAddChildOp(const mozart2::AddChildOpPtr& op);
  bool ApplyAddPartOp(const mozart2::AddPartOpPtr& op);
  bool ApplyDetachOp(const mozart2::DetachOpPtr& op);
  bool ApplyDetachChildrenOp(const mozart2::DetachChildrenOpPtr& op);
  bool ApplySetTransformOp(const mozart2::SetTransformOpPtr& op);
  bool ApplySetRotationOp(const mozart2::SetRotationOpPtr& op);
  bool ApplySetShapeOp(const mozart2::SetShapeOpPtr& op);
  bool ApplySetMaterialOp(const mozart2::SetMaterialOpPtr& op);
  bool ApplySetClipOp(const mozart2::SetClipOpPtr& op);
  bool ApplySetCameraOp(const mozart2::SetCameraOpPtr& op);
  bool ApplySetCameraProjectionOp(const mozart2::SetCameraProjectionOpPtr& op);
  bool ApplySetLightIntensityOp(const mozart2::SetLightIntensityOpPtr& op);
  bool ApplySetTextureOp(const mozart2::SetTextureOpPtr& op);
  bool ApplySetColorOp(const mozart2::SetColorOpPtr& op);

  // Resource creation functions, called by ApplyCreateResourceOp().
  bool ApplyCreateMemory(ResourceId id, const mozart2::MemoryPtr& args);
  bool ApplyCreateImage(ResourceId id, const mozart2::ImagePtr& args);
  bool ApplyCreateImagePipe(ResourceId id,
                            const mozart2::ImagePipeArgsPtr& args);
  bool ApplyCreateBuffer(ResourceId id, const mozart2::BufferPtr& args);
  bool ApplyCreateScene(ResourceId id, const mozart2::ScenePtr& args);
  bool ApplyCreateCamera(ResourceId id, const mozart2::CameraPtr& args);
  bool ApplyCreateDisplayRenderer(ResourceId id,
                                  const mozart2::DisplayRendererPtr& args);
  bool ApplyCreateImagePipeRenderer(ResourceId id,
                                    const mozart2::ImagePipeRendererPtr& args);
  bool ApplyCreateDirectionalLight(ResourceId id,
                                   const mozart2::DirectionalLightPtr& args);
  bool ApplyCreateRectangle(ResourceId id, const mozart2::RectanglePtr& args);
  bool ApplyCreateRoundedRectangle(ResourceId id,
                                   const mozart2::RoundedRectanglePtr& args);
  bool ApplyCreateCircle(ResourceId id, const mozart2::CirclePtr& args);
  bool ApplyCreateMesh(ResourceId id, const mozart2::MeshPtr& args);
  bool ApplyCreateMaterial(ResourceId id, const mozart2::MaterialPtr& args);
  bool ApplyCreateClipNode(ResourceId id, const mozart2::ClipNodePtr& args);
  bool ApplyCreateEntityNode(ResourceId id, const mozart2::EntityNodePtr& args);
  bool ApplyCreateShapeNode(ResourceId id, const mozart2::ShapeNodePtr& args);
  bool ApplyCreateTagNode(ResourceId id, const mozart2::TagNodePtr& args);
  bool ApplyCreateVariable(ResourceId id, const mozart2::VariablePtr& args);

  // Actually create resources.
  ResourcePtr CreateMemory(ResourceId, const mozart2::MemoryPtr& args);
  ResourcePtr CreateImage(ResourceId,
                          MemoryPtr memory,
                          const mozart2::ImagePtr& args);
  ResourcePtr CreateScene(ResourceId id, const mozart2::ScenePtr& args);
  ResourcePtr CreateCamera(ResourceId id, const mozart2::CameraPtr& args);
  ResourcePtr CreateDisplayRenderer(ResourceId id,
                                    const mozart2::DisplayRendererPtr& args);
  ResourcePtr CreateImagePipeRenderer(
      ResourceId id,
      const mozart2::ImagePipeRendererPtr& args);
  ResourcePtr CreateDirectionalLight(ResourceId id,
                                     escher::vec3 direction,
                                     float intensity);
  ResourcePtr CreateClipNode(ResourceId id, const mozart2::ClipNodePtr& args);
  ResourcePtr CreateEntityNode(ResourceId id,
                               const mozart2::EntityNodePtr& args);
  ResourcePtr CreateShapeNode(ResourceId id, const mozart2::ShapeNodePtr& args);
  ResourcePtr CreateTagNode(ResourceId id, const mozart2::TagNodePtr& args);
  ResourcePtr CreateCircle(ResourceId id, float initial_radius);
  ResourcePtr CreateRectangle(ResourceId id, float width, float height);
  ResourcePtr CreateRoundedRectangle(ResourceId id,
                                     float width,
                                     float height,
                                     float top_left_radius,
                                     float top_right_radius,
                                     float bottom_right_radius,
                                     float bottom_left_radius);
  ResourcePtr CreateMaterial(ResourceId id);

  // Return false and log an error if the value is not of the expected type.
  // NOTE: although failure does not halt execution of the program, it does
  // indicate client error, and will be used by the caller to tear down the
  // Session.
  bool AssertValueIsOfType(const mozart2::ValuePtr& value,
                           const mozart2::Value::Tag* tags,
                           size_t tag_count);
  template <size_t N>
  bool AssertValueIsOfType(const mozart2::ValuePtr& value,
                           const std::array<mozart2::Value::Tag, N>& tags) {
    return AssertValueIsOfType(value, tags.data(), N);
  }

  friend class Resource;
  void IncrementResourceCount() { ++resource_count_; }
  void DecrementResourceCount() { --resource_count_; }

  struct Update {
    uint64_t presentation_time;

    ::fidl::Array<mozart2::OpPtr> ops;
    ::fidl::Array<mx::event> wait_events;
    ::fidl::Array<mx::event> signal_events;

    // Callback to report when the update has been applied in response to
    // an invocation of |Session.Present()|.
    mozart2::Session::PresentCallback present_callback;
  };
  bool ApplyUpdate(Update* update);
  std::queue<Update> scheduled_updates_;

  const SessionId id_;
  SessionContext* const context_;
  ErrorReporter* error_reporter_ = nullptr;

  ResourceMap resources_;

  size_t resource_count_ = 0;
  bool is_valid_ = true;
};

}  // namespace scene
}  // namespace mozart
