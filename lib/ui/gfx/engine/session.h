// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_SESSION_H_
#define GARNET_LIB_UI_GFX_ENGINE_SESSION_H_

#include <vector>

#include <fuchsia/ui/gfx/cpp/fidl.h>

#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/resource_map.h"
#include "garnet/lib/ui/gfx/id.h"
#include "garnet/lib/ui/gfx/resources/memory.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "garnet/lib/ui/scenic/util/print_command.h"
#include "lib/escher/flib/fence_set_listener.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace scenic_impl {
namespace gfx {

class Image;
using ImagePtr = ::fxl::RefPtr<Image>;

class ImageBase;
using ImageBasePtr = ::fxl::RefPtr<ImageBase>;

class ImagePipe;
using ImagePipePtr = ::fxl::RefPtr<ImagePipe>;

class Session;
using SessionPtr = ::fxl::RefPtr<Session>;

class Engine;
class Resource;
class SessionHandler;

// TODO: use unsafe ref-counting for better performance (our architecture
// guarantees that this is safe).
class Session : public fxl::RefCountedThreadSafe<Session> {
 public:
  Session(SessionId id, Engine* engine,
          EventReporter* event_reporter = EventReporter::Default(),
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
  // ResourceId. This number is decremented when a
  // ReleaseResourceCmd is applied.  However, the resource may continue to
  // exist if it is referenced by other resources.
  size_t GetMappedResourceCount() const { return resources_.size(); }

  // Session becomes invalid once TearDown is called.
  bool is_valid() const { return is_valid_; }

  ErrorReporter* error_reporter() const;  // Never nullptr.
  EventReporter* event_reporter() const;  // Never nullptr.

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

  // Convenience.  Forwards an event to the EventReporter.
  void EnqueueEvent(::fuchsia::ui::gfx::Event event);
  void EnqueueEvent(::fuchsia::ui::input::InputEvent event);

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

  // Cmderation application functions, called by ApplyCommand().
  bool ApplyCreateResourceCmd(::fuchsia::ui::gfx::CreateResourceCmd command);
  bool ApplyReleaseResourceCmd(::fuchsia::ui::gfx::ReleaseResourceCmd command);
  bool ApplyExportResourceCmd(::fuchsia::ui::gfx::ExportResourceCmd command);
  bool ApplyImportResourceCmd(::fuchsia::ui::gfx::ImportResourceCmd command);
  bool ApplyAddChildCmd(::fuchsia::ui::gfx::AddChildCmd command);
  bool ApplyAddPartCmd(::fuchsia::ui::gfx::AddPartCmd command);
  bool ApplyDetachCmd(::fuchsia::ui::gfx::DetachCmd command);
  bool ApplyDetachChildrenCmd(::fuchsia::ui::gfx::DetachChildrenCmd command);
  bool ApplySetTagCmd(::fuchsia::ui::gfx::SetTagCmd command);
  bool ApplySetTranslationCmd(::fuchsia::ui::gfx::SetTranslationCmd command);
  bool ApplySetScaleCmd(::fuchsia::ui::gfx::SetScaleCmd command);
  bool ApplySetRotationCmd(::fuchsia::ui::gfx::SetRotationCmd command);
  bool ApplySetAnchorCmd(::fuchsia::ui::gfx::SetAnchorCmd command);
  bool ApplySetSizeCmd(::fuchsia::ui::gfx::SetSizeCmd command);
  bool ApplySetOpacityCmd(::fuchsia::ui::gfx::SetOpacityCmd command);
  bool ApplySendSizeChangeHintCmd(
      ::fuchsia::ui::gfx::SendSizeChangeHintCmdHACK command);
  bool ApplySetShapeCmd(::fuchsia::ui::gfx::SetShapeCmd command);
  bool ApplySetMaterialCmd(::fuchsia::ui::gfx::SetMaterialCmd command);
  bool ApplySetClipCmd(::fuchsia::ui::gfx::SetClipCmd command);
  bool ApplySetViewPropertiesCmd(
      ::fuchsia::ui::gfx::SetViewPropertiesCmd command);
  bool ApplySetHitTestBehaviorCmd(
      ::fuchsia::ui::gfx::SetHitTestBehaviorCmd command);
  bool ApplySetCameraCmd(::fuchsia::ui::gfx::SetCameraCmd command);
  bool ApplySetCameraTransformCmd(
      ::fuchsia::ui::gfx::SetCameraTransformCmd command);
  bool ApplySetCameraProjectionCmd(
      ::fuchsia::ui::gfx::SetCameraProjectionCmd command);
  bool ApplySetStereoCameraProjectionCmd(
      ::fuchsia::ui::gfx::SetStereoCameraProjectionCmd command);
  bool ApplySetCameraPoseBufferCmd(
      ::fuchsia::ui::gfx::SetCameraPoseBufferCmd command);
  bool ApplySetLightColorCmd(::fuchsia::ui::gfx::SetLightColorCmd command);
  bool ApplySetLightDirectionCmd(
      ::fuchsia::ui::gfx::SetLightDirectionCmd command);
  bool ApplyAddLightCmd(::fuchsia::ui::gfx::AddLightCmd command);
  bool ApplyDetachLightCmd(::fuchsia::ui::gfx::DetachLightCmd command);
  bool ApplyDetachLightsCmd(::fuchsia::ui::gfx::DetachLightsCmd command);
  bool ApplySetTextureCmd(::fuchsia::ui::gfx::SetTextureCmd command);
  bool ApplySetColorCmd(::fuchsia::ui::gfx::SetColorCmd command);
  bool ApplyBindMeshBuffersCmd(::fuchsia::ui::gfx::BindMeshBuffersCmd command);
  bool ApplyAddLayerCmd(::fuchsia::ui::gfx::AddLayerCmd command);
  bool ApplyRemoveLayerCmd(::fuchsia::ui::gfx::RemoveLayerCmd command);
  bool ApplyRemoveAllLayersCmd(::fuchsia::ui::gfx::RemoveAllLayersCmd command);
  bool ApplySetLayerStackCmd(::fuchsia::ui::gfx::SetLayerStackCmd command);
  bool ApplySetRendererCmd(::fuchsia::ui::gfx::SetRendererCmd command);
  bool ApplySetRendererParamCmd(
      ::fuchsia::ui::gfx::SetRendererParamCmd command);
  bool ApplySetEventMaskCmd(::fuchsia::ui::gfx::SetEventMaskCmd command);
  bool ApplySetLabelCmd(::fuchsia::ui::gfx::SetLabelCmd command);
  bool ApplySetDisableClippingCmd(
      ::fuchsia::ui::gfx::SetDisableClippingCmd command);

  // Resource creation functions, called by ApplyCreateResourceCmd().
  bool ApplyCreateMemory(ResourceId id, ::fuchsia::ui::gfx::MemoryArgs args);
  bool ApplyCreateImage(ResourceId id, ::fuchsia::ui::gfx::ImageArgs args);
  bool ApplyCreateImagePipe(ResourceId id,
                            ::fuchsia::ui::gfx::ImagePipeArgs args);
  bool ApplyCreateBuffer(ResourceId id, ::fuchsia::ui::gfx::BufferArgs args);
  bool ApplyCreateScene(ResourceId id, ::fuchsia::ui::gfx::SceneArgs args);
  bool ApplyCreateCamera(ResourceId id, ::fuchsia::ui::gfx::CameraArgs args);
  bool ApplyCreateStereoCamera(ResourceId id,
                               ::fuchsia::ui::gfx::StereoCameraArgs args);
  bool ApplyCreateRenderer(ResourceId id,
                           ::fuchsia::ui::gfx::RendererArgs args);
  bool ApplyCreateAmbientLight(ResourceId id,
                               ::fuchsia::ui::gfx::AmbientLightArgs args);
  bool ApplyCreateDirectionalLight(
      ResourceId id, ::fuchsia::ui::gfx::DirectionalLightArgs args);
  bool ApplyCreateRectangle(ResourceId id,
                            ::fuchsia::ui::gfx::RectangleArgs args);
  bool ApplyCreateRoundedRectangle(
      ResourceId id, ::fuchsia::ui::gfx::RoundedRectangleArgs args);
  bool ApplyCreateCircle(ResourceId id, ::fuchsia::ui::gfx::CircleArgs args);
  bool ApplyCreateMesh(ResourceId id, ::fuchsia::ui::gfx::MeshArgs args);
  bool ApplyCreateMaterial(ResourceId id,
                           ::fuchsia::ui::gfx::MaterialArgs args);
  bool ApplyCreateView(ResourceId id, ::fuchsia::ui::gfx::ViewArgs args);
  bool ApplyCreateViewHolder(ResourceId id,
                             ::fuchsia::ui::gfx::ViewHolderArgs args);
  bool ApplyCreateClipNode(ResourceId id,
                           ::fuchsia::ui::gfx::ClipNodeArgs args);
  bool ApplyCreateEntityNode(ResourceId id,
                             ::fuchsia::ui::gfx::EntityNodeArgs args);
  bool ApplyCreateOpacityNode(ResourceId id,
                              ::fuchsia::ui::gfx::OpacityNodeArgs args);
  bool ApplyCreateShapeNode(ResourceId id,
                            ::fuchsia::ui::gfx::ShapeNodeArgs args);
  bool ApplyCreateCompositor(ResourceId id,
                             ::fuchsia::ui::gfx::CompositorArgs args);
  bool ApplyCreateDisplayCompositor(
      ResourceId id, ::fuchsia::ui::gfx::DisplayCompositorArgs args);
  bool ApplyCreateImagePipeCompositor(
      ResourceId id, ::fuchsia::ui::gfx::ImagePipeCompositorArgs args);
  bool ApplyCreateLayerStack(ResourceId id,
                             ::fuchsia::ui::gfx::LayerStackArgs args);
  bool ApplyCreateLayer(ResourceId id, ::fuchsia::ui::gfx::LayerArgs args);
  bool ApplyCreateVariable(ResourceId id,
                           ::fuchsia::ui::gfx::VariableArgs args);
  bool ApplyTakeSnapshotCmdHACK(
      ::fuchsia::ui::gfx::TakeSnapshotCmdHACK command);

  // Actually create resources.
  ResourcePtr CreateMemory(ResourceId id, ::fuchsia::ui::gfx::MemoryArgs args);
  ResourcePtr CreateImage(ResourceId id, MemoryPtr memory,
                          ::fuchsia::ui::gfx::ImageArgs args);
  ResourcePtr CreateBuffer(ResourceId id, MemoryPtr memory,
                           uint32_t memory_offset, uint32_t num_bytes);

  ResourcePtr CreateScene(ResourceId id, ::fuchsia::ui::gfx::SceneArgs args);
  ResourcePtr CreateCamera(ResourceId id, ::fuchsia::ui::gfx::CameraArgs args);
  ResourcePtr CreateStereoCamera(ResourceId id,
                                 ::fuchsia::ui::gfx::StereoCameraArgs args);
  ResourcePtr CreateRenderer(ResourceId id,
                             ::fuchsia::ui::gfx::RendererArgs args);

  ResourcePtr CreateAmbientLight(ResourceId id);
  ResourcePtr CreateDirectionalLight(ResourceId id);

  ResourcePtr CreateView(ResourceId id, ::fuchsia::ui::gfx::ViewArgs args);
  ResourcePtr CreateViewHolder(ResourceId id,
                               ::fuchsia::ui::gfx::ViewHolderArgs args);
  ResourcePtr CreateClipNode(ResourceId id,
                             ::fuchsia::ui::gfx::ClipNodeArgs args);
  ResourcePtr CreateEntityNode(ResourceId id,
                               ::fuchsia::ui::gfx::EntityNodeArgs args);
  ResourcePtr CreateOpacityNode(ResourceId id,
                                ::fuchsia::ui::gfx::OpacityNodeArgs args);
  ResourcePtr CreateShapeNode(ResourceId id,
                              ::fuchsia::ui::gfx::ShapeNodeArgs args);

  ResourcePtr CreateCompositor(ResourceId id,
                               ::fuchsia::ui::gfx::CompositorArgs args);
  ResourcePtr CreateDisplayCompositor(
      ResourceId id, ::fuchsia::ui::gfx::DisplayCompositorArgs args);
  ResourcePtr CreateImagePipeCompositor(
      ResourceId id, ::fuchsia::ui::gfx::ImagePipeCompositorArgs args);
  ResourcePtr CreateLayerStack(ResourceId id,
                               ::fuchsia::ui::gfx::LayerStackArgs args);
  ResourcePtr CreateLayer(ResourceId id, ::fuchsia::ui::gfx::LayerArgs args);
  ResourcePtr CreateCircle(ResourceId id, float initial_radius);
  ResourcePtr CreateRectangle(ResourceId id, float width, float height);
  ResourcePtr CreateRoundedRectangle(ResourceId id, float width, float height,
                                     float top_left_radius,
                                     float top_right_radius,
                                     float bottom_right_radius,
                                     float bottom_left_radius);
  ResourcePtr CreateMesh(ResourceId id);
  ResourcePtr CreateMaterial(ResourceId id);
  ResourcePtr CreateVariable(ResourceId id,
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

    bool operator>(const ImagePipeUpdate& rhs) const {
      return presentation_time > rhs.presentation_time;
    }
  };
  // The least element should be on top.
  std::priority_queue<ImagePipeUpdate, std::vector<ImagePipeUpdate>,
                      std::greater<ImagePipeUpdate>>
      scheduled_image_pipe_updates_;

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
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_SESSION_H_
