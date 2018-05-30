// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_SESSION_H_
#define GARNET_LIB_UI_GFX_ENGINE_SESSION_H_

#include <vector>

#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/resource_map.h"
#include "garnet/lib/ui/gfx/resources/memory.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "garnet/lib/ui/scenic/util/print_command.h"
#include "lib/escher/flib/fence_set_listener.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace scenic {
namespace gfx {

using SessionId = ::scenic::SessionId;

class Image;
using ImagePtr = ::fxl::RefPtr<Image>;

class ImageBase;
using ImageBasePtr = ::fxl::RefPtr<ImageBase>;

class ImagePipe;
using ImagePipePtr = ::fxl::RefPtr<ImagePipe>;

class Session;
using SessionPtr = ::fxl::RefPtr<Session>;

class Engine;
class SessionHandler;

// TODO: use unsafe ref-counting for better performance (our architecture
// guarantees that this is safe).
class Session : public fxl::RefCountedThreadSafe<Session> {
 public:
  Session(SessionId id, Engine* engine, EventReporter* event_reporter = nullptr,
          ErrorReporter* error_reporter = ErrorReporter::Default());
  virtual ~Session();

  // Apply the operation to the current session state.  Return true if
  // successful, and false if the op is somehow invalid.  In the latter case,
  // the Session is left unchanged.
  bool ApplyCommand(::fuchsia::ui::gfx::Command command);

  SessionId id() const { return id_; }
  Engine* engine() const { return engine_; }
  escher::Escher* escher() const { return engine_->escher(); }

  // Return the total number of existing resources associated with this Session.
  size_t GetTotalResourceCount() const { return resource_count_; }

  // Return the number of resources that a client can identify via a
  // scenic::ResourceId. This number is decremented when a
  // ReleaseResourceCommand is applied.  However, the resource may continue to
  // exist if it is referenced by other resources.
  size_t GetMappedResourceCount() const { return resources_.size(); }

  // Session becomes invalid once TearDown is called.
  bool is_valid() const { return is_valid_; }

  ErrorReporter* error_reporter() const;

  ResourceMap* resources() { return &resources_; }

  // Called by SessionHandler::Present().  Stashes the arguments without
  // applying them; they will later be applied by ApplyScheduledUpdates().
  bool ScheduleUpdate(uint64_t presentation_time,
                      std::vector<::fuchsia::ui::gfx::Command> commands,
                      ::fidl::VectorPtr<zx::event> acquire_fences,
                      ::fidl::VectorPtr<zx::event> release_fences,
                      fuchsia::ui::scenic::Session::PresentCallback callback);

  // Called by ImagePipe::PresentImage().  Stashes the arguments without
  // applying them; they will later be applied by ApplyScheduledUpdates().
  void ScheduleImagePipeUpdate(uint64_t presentation_time,
                               ImagePipePtr image_pipe);

  // Called by Engine() when it is notified by the FrameScheduler that
  // a frame should be rendered for the specified |presentation_time|.  Return
  // true if any updates were applied, and false otherwise.
  bool ApplyScheduledUpdates(uint64_t presentation_time,
                             uint64_t presentation_interval);

  // Convenience.  Wraps a ::fuchsia::ui::gfx::Event in a ui::Event, then
  // forwards it to the EventReporter.
  void EnqueueEvent(::fuchsia::ui::gfx::Event event);

  // Called by SessionHandler::HitTest().
  void HitTest(uint32_t node_id, ::fuchsia::ui::gfx::vec3 ray_origin,
               ::fuchsia::ui::gfx::vec3 ray_direction,
               fuchsia::ui::scenic::Session::HitTestCallback callback);

  // Called by SessionHandler::HitTestDeviceRay().
  void HitTestDeviceRay(::fuchsia::ui::gfx::vec3 ray_origin,
                        ::fuchsia::ui::gfx::vec3 ray_direction,
                        fuchsia::ui::scenic::Session::HitTestCallback callback);

 protected:
  friend class SessionHandler;
  // Called only by SessionHandler. Use BeginTearDown() instead when you need to
  // teardown from within Session. Virtual to allow test subclasses to override.
  //
  // The chain of events is:
  // Session::BeginTearDown or SessionHandler::BeginTearDown
  // => Engine::TearDownSession
  // => SessionHandler::TearDown
  // => Session::TearDown
  //
  // We are guaranteed that by the time TearDown() is closed, SessionHandler
  // has destroyed the channel to this session.
  virtual void TearDown();

 private:
  // Called internally to initiate teardown.
  void BeginTearDown();

  // Commanderation application functions, called by ApplyCommand().
  bool ApplyCreateResourceCommand(
      ::fuchsia::ui::gfx::CreateResourceCommand command);
  bool ApplyReleaseResourceCommand(
      ::fuchsia::ui::gfx::ReleaseResourceCommand command);
  bool ApplyExportResourceCommand(
      ::fuchsia::ui::gfx::ExportResourceCommand command);
  bool ApplyImportResourceCommand(
      ::fuchsia::ui::gfx::ImportResourceCommand command);
  bool ApplyAddChildCommand(::fuchsia::ui::gfx::AddChildCommand command);
  bool ApplyAddPartCommand(::fuchsia::ui::gfx::AddPartCommand command);
  bool ApplyDetachCommand(::fuchsia::ui::gfx::DetachCommand command);
  bool ApplyDetachChildrenCommand(
      ::fuchsia::ui::gfx::DetachChildrenCommand command);
  bool ApplySetTagCommand(::fuchsia::ui::gfx::SetTagCommand command);
  bool ApplySetTranslationCommand(
      ::fuchsia::ui::gfx::SetTranslationCommand command);
  bool ApplySetScaleCommand(::fuchsia::ui::gfx::SetScaleCommand command);
  bool ApplySetRotationCommand(::fuchsia::ui::gfx::SetRotationCommand command);
  bool ApplySetAnchorCommand(::fuchsia::ui::gfx::SetAnchorCommand command);
  bool ApplySetSizeCommand(::fuchsia::ui::gfx::SetSizeCommand command);
  bool ApplySetShapeCommand(::fuchsia::ui::gfx::SetShapeCommand command);
  bool ApplySetMaterialCommand(::fuchsia::ui::gfx::SetMaterialCommand command);
  bool ApplySetClipCommand(::fuchsia::ui::gfx::SetClipCommand command);
  bool ApplySetSpacePropertiesCommand(
      ::fuchsia::ui::gfx::SetSpacePropertiesCommand command);
  bool ApplySetHitTestBehaviorCommand(
      ::fuchsia::ui::gfx::SetHitTestBehaviorCommand command);
  bool ApplySetCameraCommand(::fuchsia::ui::gfx::SetCameraCommand command);
  bool ApplySetCameraTransformCommand(
      ::fuchsia::ui::gfx::SetCameraTransformCommand command);
  bool ApplySetCameraProjectionCommand(
      ::fuchsia::ui::gfx::SetCameraProjectionCommand command);
  bool ApplySetStereoCameraProjectionCommand(
      ::fuchsia::ui::gfx::SetStereoCameraProjectionCommand command);
  bool ApplySetCameraPoseBufferCommand(
      ::fuchsia::ui::gfx::SetCameraPoseBufferCommand command);
  bool ApplySetLightColorCommand(
      ::fuchsia::ui::gfx::SetLightColorCommand command);
  bool ApplySetLightDirectionCommand(
      ::fuchsia::ui::gfx::SetLightDirectionCommand command);
  bool ApplyAddLightCommand(::fuchsia::ui::gfx::AddLightCommand command);
  bool ApplyDetachLightCommand(::fuchsia::ui::gfx::DetachLightCommand command);
  bool ApplyDetachLightsCommand(
      ::fuchsia::ui::gfx::DetachLightsCommand command);
  bool ApplySetTextureCommand(::fuchsia::ui::gfx::SetTextureCommand command);
  bool ApplySetColorCommand(::fuchsia::ui::gfx::SetColorCommand command);
  bool ApplyBindMeshBuffersCommand(
      ::fuchsia::ui::gfx::BindMeshBuffersCommand command);
  bool ApplyAddLayerCommand(::fuchsia::ui::gfx::AddLayerCommand command);
  bool ApplyRemoveLayerCommand(::fuchsia::ui::gfx::RemoveLayerCommand command);
  bool ApplyRemoveAllLayersCommand(
      ::fuchsia::ui::gfx::RemoveAllLayersCommand command);
  bool ApplySetLayerStackCommand(
      ::fuchsia::ui::gfx::SetLayerStackCommand command);
  bool ApplySetRendererCommand(::fuchsia::ui::gfx::SetRendererCommand command);
  bool ApplySetRendererParamCommand(
      ::fuchsia::ui::gfx::SetRendererParamCommand command);
  bool ApplySetEventMaskCommand(
      ::fuchsia::ui::gfx::SetEventMaskCommand command);
  bool ApplySetLabelCommand(::fuchsia::ui::gfx::SetLabelCommand command);
  bool ApplySetDisableClippingCommand(
      ::fuchsia::ui::gfx::SetDisableClippingCommand command);

  // Resource creation functions, called by ApplyCreateResourceCommand().
  bool ApplyCreateMemory(scenic::ResourceId id,
                         ::fuchsia::ui::gfx::MemoryArgs args);
  bool ApplyCreateImage(scenic::ResourceId id,
                        ::fuchsia::ui::gfx::ImageArgs args);
  bool ApplyCreateImagePipe(scenic::ResourceId id,
                            ::fuchsia::ui::gfx::ImagePipeArgs args);
  bool ApplyCreateBuffer(scenic::ResourceId id,
                         ::fuchsia::ui::gfx::BufferArgs args);
  bool ApplyCreateScene(scenic::ResourceId id,
                        ::fuchsia::ui::gfx::SceneArgs args);
  bool ApplyCreateCamera(scenic::ResourceId id,
                         ::fuchsia::ui::gfx::CameraArgs args);
  bool ApplyCreateStereoCamera(scenic::ResourceId id,
                               ::fuchsia::ui::gfx::StereoCameraArgs args);
  bool ApplyCreateRenderer(scenic::ResourceId id,
                           ::fuchsia::ui::gfx::RendererArgs args);
  bool ApplyCreateAmbientLight(scenic::ResourceId id,
                               ::fuchsia::ui::gfx::AmbientLightArgs args);
  bool ApplyCreateDirectionalLight(
      scenic::ResourceId id, ::fuchsia::ui::gfx::DirectionalLightArgs args);
  bool ApplyCreateRectangle(scenic::ResourceId id,
                            ::fuchsia::ui::gfx::RectangleArgs args);
  bool ApplyCreateRoundedRectangle(
      scenic::ResourceId id, ::fuchsia::ui::gfx::RoundedRectangleArgs args);
  bool ApplyCreateCircle(scenic::ResourceId id,
                         ::fuchsia::ui::gfx::CircleArgs args);
  bool ApplyCreateMesh(scenic::ResourceId id,
                       ::fuchsia::ui::gfx::MeshArgs args);
  bool ApplyCreateMaterial(scenic::ResourceId id,
                           ::fuchsia::ui::gfx::MaterialArgs args);
  bool ApplyCreateClipNode(scenic::ResourceId id,
                           ::fuchsia::ui::gfx::ClipNodeArgs args);
  bool ApplyCreateEntityNode(scenic::ResourceId id,
                             ::fuchsia::ui::gfx::EntityNodeArgs args);
  bool ApplyCreateShapeNode(scenic::ResourceId id,
                            ::fuchsia::ui::gfx::ShapeNodeArgs args);
  bool ApplyCreateSpace(scenic::ResourceId id,
                        ::fuchsia::ui::gfx::SpaceArgs args);
  bool ApplyCreateSpaceHolder(scenic::ResourceId id,
                              ::fuchsia::ui::gfx::SpaceHolderArgs args);
  bool ApplyCreateDisplayCompositor(
      scenic::ResourceId id, ::fuchsia::ui::gfx::DisplayCompositorArgs args);
  bool ApplyCreateImagePipeCompositor(
      scenic::ResourceId id, ::fuchsia::ui::gfx::ImagePipeCompositorArgs args);
  bool ApplyCreateLayerStack(scenic::ResourceId id,
                             ::fuchsia::ui::gfx::LayerStackArgs args);
  bool ApplyCreateLayer(scenic::ResourceId id,
                        ::fuchsia::ui::gfx::LayerArgs args);
  bool ApplyCreateVariable(scenic::ResourceId id,
                           ::fuchsia::ui::gfx::VariableArgs args);

  // Actually create resources.
  ResourcePtr CreateMemory(scenic::ResourceId id,
                           ::fuchsia::ui::gfx::MemoryArgs args);
  ResourcePtr CreateImage(scenic::ResourceId id, MemoryPtr memory,
                          ::fuchsia::ui::gfx::ImageArgs args);
  ResourcePtr CreateBuffer(scenic::ResourceId id, MemoryPtr memory,
                           uint32_t memory_offset, uint32_t num_bytes);

  ResourcePtr CreateScene(scenic::ResourceId id,
                          ::fuchsia::ui::gfx::SceneArgs args);
  ResourcePtr CreateCamera(scenic::ResourceId id,
                           ::fuchsia::ui::gfx::CameraArgs args);
  ResourcePtr CreateStereoCamera(scenic::ResourceId id,
                                 ::fuchsia::ui::gfx::StereoCameraArgs args);
  ResourcePtr CreateRenderer(scenic::ResourceId id,
                             ::fuchsia::ui::gfx::RendererArgs args);

  ResourcePtr CreateAmbientLight(scenic::ResourceId id);
  ResourcePtr CreateDirectionalLight(scenic::ResourceId id);
  ResourcePtr CreateClipNode(scenic::ResourceId id,
                             ::fuchsia::ui::gfx::ClipNodeArgs args);
  ResourcePtr CreateEntityNode(scenic::ResourceId id,
                               ::fuchsia::ui::gfx::EntityNodeArgs args);
  ResourcePtr CreateShapeNode(scenic::ResourceId id,
                              ::fuchsia::ui::gfx::ShapeNodeArgs args);
  ResourcePtr CreateDisplayCompositor(
      scenic::ResourceId id, ::fuchsia::ui::gfx::DisplayCompositorArgs args);
  ResourcePtr CreateImagePipeCompositor(
      scenic::ResourceId id, ::fuchsia::ui::gfx::ImagePipeCompositorArgs args);
  ResourcePtr CreateLayerStack(scenic::ResourceId id,
                               ::fuchsia::ui::gfx::LayerStackArgs args);
  ResourcePtr CreateLayer(scenic::ResourceId id,
                          ::fuchsia::ui::gfx::LayerArgs args);
  ResourcePtr CreateCircle(scenic::ResourceId id, float initial_radius);
  ResourcePtr CreateRectangle(scenic::ResourceId id, float width, float height);
  ResourcePtr CreateRoundedRectangle(scenic::ResourceId id, float width,
                                     float height, float top_left_radius,
                                     float top_right_radius,
                                     float bottom_right_radius,
                                     float bottom_left_radius);
  ResourcePtr CreateMesh(scenic::ResourceId id);
  ResourcePtr CreateMaterial(scenic::ResourceId id);
  ResourcePtr CreateVariable(scenic::ResourceId id,
                             ::fuchsia::ui::gfx::VariableArgs args);

  // Return false and log an error if the value is not of the expected type.
  // NOTE: although failure does not halt execution of the program, it does
  // indicate client error, and will be used by the caller to tear down the
  // Session.
  bool AssertValueIsOfType(const ::fuchsia::ui::gfx::Value& value,
                           const ::fuchsia::ui::gfx::Value::Tag* tags,
                           size_t tag_count);
  template <size_t N>
  bool AssertValueIsOfType(
      const ::fuchsia::ui::gfx::Value& value,
      const std::array<::fuchsia::ui::gfx::Value::Tag, N>& tags) {
    return AssertValueIsOfType(value, tags.data(), N);
  }

  friend class Resource;
  void IncrementResourceCount() { ++resource_count_; }
  void DecrementResourceCount() { --resource_count_; }

  struct Update {
    uint64_t presentation_time;

    std::vector<::fuchsia::ui::gfx::Command> commands;
    std::unique_ptr<escher::FenceSetListener> acquire_fences;
    ::fidl::VectorPtr<zx::event> release_fences;

    // Callback to report when the update has been applied in response to
    // an invocation of |Session.Present()|.
    fuchsia::ui::scenic::Session::PresentCallback present_callback;
  };
  bool ApplyUpdate(std::vector<::fuchsia::ui::gfx::Command> commands);
  std::queue<Update> scheduled_updates_;
  ::fidl::VectorPtr<zx::event> fences_to_release_on_next_update_;

  uint64_t last_applied_update_presentation_time_ = 0;
  uint64_t last_presentation_time_ = 0;

  struct ImagePipeUpdate {
    uint64_t presentation_time;
    ImagePipePtr image_pipe;

    bool operator<(const ImagePipeUpdate& rhs) const {
      return presentation_time < rhs.presentation_time;
    }
  };
  std::priority_queue<ImagePipeUpdate> scheduled_image_pipe_updates_;

  const SessionId id_;
  Engine* const engine_;
  ErrorReporter* error_reporter_ = nullptr;
  EventReporter* event_reporter_ = nullptr;

  ResourceMap resources_;

  size_t resource_count_ = 0;
  bool is_valid_ = true;

  fxl::WeakPtrFactory<Session> weak_factory_;  // must be last
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_ENGINE_SESSION_H_
