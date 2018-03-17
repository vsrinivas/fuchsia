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

using SessionId = uint64_t;

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
  Session(SessionId id,
          Engine* engine,
          EventReporter* event_reporter = nullptr,
          ErrorReporter* error_reporter = ErrorReporter::Default());
  virtual ~Session();

  // Apply the operation to the current session state.  Return true if
  // successful, and false if the op is somehow invalid.  In the latter case,
  // the Session is left unchanged.
  bool ApplyCommand(const ui::gfx::CommandPtr& command);

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
                      ::f1dl::Array<ui::gfx::CommandPtr> commands,
                      ::f1dl::Array<zx::event> acquire_fences,
                      ::f1dl::Array<zx::event> release_fences,
                      const ui::Session::PresentCallback& callback);

  // Called by ImagePipe::PresentImage().  Stashes the arguments without
  // applying them; they will later be applied by ApplyScheduledUpdates().
  void ScheduleImagePipeUpdate(uint64_t presentation_time,
                               ImagePipePtr image_pipe);

  // Called by Engine() when it is notified by the FrameScheduler that
  // a frame should be rendered for the specified |presentation_time|.  Return
  // true if any updates were applied, and false otherwise.
  bool ApplyScheduledUpdates(uint64_t presentation_time,
                             uint64_t presentation_interval);

  // Add an event to our queue, which will be scheduled to be flushed and sent
  // to the event reporter later.
  void EnqueueEvent(ui::gfx::EventPtr event);

  // Called by SessionHandler::HitTest().
  void HitTest(uint32_t node_id,
               ui::gfx::vec3Ptr ray_origin,
               ui::gfx::vec3Ptr ray_direction,
               const ui::Session::HitTestCallback& callback);

  // Called by SessionHandler::HitTestDeviceRay().
  void HitTestDeviceRay(ui::gfx::vec3Ptr ray_origin,
                        ui::gfx::vec3Ptr ray_direction,
                        const ui::Session::HitTestCallback& callback);

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
      const ui::gfx::CreateResourceCommandPtr& command);
  bool ApplyReleaseResourceCommand(
      const ui::gfx::ReleaseResourceCommandPtr& command);
  bool ApplyExportResourceCommand(
      const ui::gfx::ExportResourceCommandPtr& command);
  bool ApplyImportResourceCommand(
      const ui::gfx::ImportResourceCommandPtr& command);
  bool ApplyAddChildCommand(const ui::gfx::AddChildCommandPtr& command);
  bool ApplyAddPartCommand(const ui::gfx::AddPartCommandPtr& command);
  bool ApplyDetachCommand(const ui::gfx::DetachCommandPtr& command);
  bool ApplyDetachChildrenCommand(
      const ui::gfx::DetachChildrenCommandPtr& command);
  bool ApplySetTagCommand(const ui::gfx::SetTagCommandPtr& command);
  bool ApplySetTranslationCommand(
      const ui::gfx::SetTranslationCommandPtr& command);
  bool ApplySetScaleCommand(const ui::gfx::SetScaleCommandPtr& command);
  bool ApplySetRotationCommand(const ui::gfx::SetRotationCommandPtr& command);
  bool ApplySetAnchorCommand(const ui::gfx::SetAnchorCommandPtr& command);
  bool ApplySetSizeCommand(const ui::gfx::SetSizeCommandPtr& command);
  bool ApplySetShapeCommand(const ui::gfx::SetShapeCommandPtr& command);
  bool ApplySetMaterialCommand(const ui::gfx::SetMaterialCommandPtr& command);
  bool ApplySetClipCommand(const ui::gfx::SetClipCommandPtr& command);
  bool ApplySetHitTestBehaviorCommand(
      const ui::gfx::SetHitTestBehaviorCommandPtr& command);
  bool ApplySetCameraCommand(const ui::gfx::SetCameraCommandPtr& command);
  bool ApplySetCameraProjectionCommand(
      const ui::gfx::SetCameraProjectionCommandPtr& command);
  bool ApplySetCameraPoseBufferCommand(
      const ui::gfx::SetCameraPoseBufferCommandPtr& command);
  bool ApplySetLightColorCommand(
      const ui::gfx::SetLightColorCommandPtr& command);
  bool ApplySetLightDirectionCommand(
      const ui::gfx::SetLightDirectionCommandPtr& command);
  bool ApplyAddLightCommand(const ui::gfx::AddLightCommandPtr& command);
  bool ApplyDetachLightCommand(const ui::gfx::DetachLightCommandPtr& command);
  bool ApplyDetachLightsCommand(const ui::gfx::DetachLightsCommandPtr& command);
  bool ApplySetTextureCommand(const ui::gfx::SetTextureCommandPtr& command);
  bool ApplySetColorCommand(const ui::gfx::SetColorCommandPtr& command);
  bool ApplyBindMeshBuffersCommand(
      const ui::gfx::BindMeshBuffersCommandPtr& command);
  bool ApplyAddLayerCommand(const ui::gfx::AddLayerCommandPtr& command);
  bool ApplySetLayerStackCommand(
      const ui::gfx::SetLayerStackCommandPtr& command);
  bool ApplySetRendererCommand(const ui::gfx::SetRendererCommandPtr& command);
  bool ApplySetRendererParamCommand(
      const ui::gfx::SetRendererParamCommandPtr& command);
  bool ApplySetEventMaskCommand(const ui::gfx::SetEventMaskCommandPtr& command);
  bool ApplySetLabelCommand(const ui::gfx::SetLabelCommandPtr& command);
  bool ApplySetDisableClippingCommand(
      const ui::gfx::SetDisableClippingCommandPtr& command);

  // Resource creation functions, called by ApplyCreateResourceCommand().
  bool ApplyCreateMemory(scenic::ResourceId id,
                         const ui::gfx::MemoryArgsPtr& args);
  bool ApplyCreateImage(scenic::ResourceId id,
                        const ui::gfx::ImageArgsPtr& args);
  bool ApplyCreateImagePipe(scenic::ResourceId id,
                            const ui::gfx::ImagePipeArgsPtr& args);
  bool ApplyCreateBuffer(scenic::ResourceId id,
                         const ui::gfx::BufferArgsPtr& args);
  bool ApplyCreateScene(scenic::ResourceId id,
                        const ui::gfx::SceneArgsPtr& args);
  bool ApplyCreateCamera(scenic::ResourceId id,
                         const ui::gfx::CameraArgsPtr& args);
  bool ApplyCreateRenderer(scenic::ResourceId id,
                           const ui::gfx::RendererArgsPtr& args);
  bool ApplyCreateAmbientLight(scenic::ResourceId id,
                               const ui::gfx::AmbientLightArgsPtr& args);
  bool ApplyCreateDirectionalLight(
      scenic::ResourceId id,
      const ui::gfx::DirectionalLightArgsPtr& args);
  bool ApplyCreateRectangle(scenic::ResourceId id,
                            const ui::gfx::RectangleArgsPtr& args);
  bool ApplyCreateRoundedRectangle(
      scenic::ResourceId id,
      const ui::gfx::RoundedRectangleArgsPtr& args);
  bool ApplyCreateCircle(scenic::ResourceId id,
                         const ui::gfx::CircleArgsPtr& args);
  bool ApplyCreateMesh(scenic::ResourceId id, const ui::gfx::MeshArgsPtr& args);
  bool ApplyCreateMaterial(scenic::ResourceId id,
                           const ui::gfx::MaterialArgsPtr& args);
  bool ApplyCreateClipNode(scenic::ResourceId id,
                           const ui::gfx::ClipNodeArgsPtr& args);
  bool ApplyCreateEntityNode(scenic::ResourceId id,
                             const ui::gfx::EntityNodeArgsPtr& args);
  bool ApplyCreateShapeNode(scenic::ResourceId id,
                            const ui::gfx::ShapeNodeArgsPtr& args);
  bool ApplyCreateDisplayCompositor(
      scenic::ResourceId id,
      const ui::gfx::DisplayCompositorArgsPtr& args);
  bool ApplyCreateImagePipeCompositor(
      scenic::ResourceId id,
      const ui::gfx::ImagePipeCompositorArgsPtr& args);
  bool ApplyCreateLayerStack(scenic::ResourceId id,
                             const ui::gfx::LayerStackArgsPtr& args);
  bool ApplyCreateLayer(scenic::ResourceId id,
                        const ui::gfx::LayerArgsPtr& args);
  bool ApplyCreateVariable(scenic::ResourceId id,
                           const ui::gfx::VariableArgsPtr& args);

  // Actually create resources.
  ResourcePtr CreateMemory(scenic::ResourceId id,
                           const ui::gfx::MemoryArgsPtr& args);
  ResourcePtr CreateImage(scenic::ResourceId id,
                          MemoryPtr memory,
                          const ui::gfx::ImageArgsPtr& args);
  ResourcePtr CreateBuffer(scenic::ResourceId id,
                           MemoryPtr memory,
                           uint32_t memory_offset,
                           uint32_t num_bytes);
  ResourcePtr CreateScene(scenic::ResourceId id,
                          const ui::gfx::SceneArgsPtr& args);
  ResourcePtr CreateCamera(scenic::ResourceId id,
                           const ui::gfx::CameraArgsPtr& args);
  ResourcePtr CreateRenderer(scenic::ResourceId id,
                             const ui::gfx::RendererArgsPtr& args);
  ResourcePtr CreateAmbientLight(scenic::ResourceId id);
  ResourcePtr CreateDirectionalLight(scenic::ResourceId id);
  ResourcePtr CreateClipNode(scenic::ResourceId id,
                             const ui::gfx::ClipNodeArgsPtr& args);
  ResourcePtr CreateEntityNode(scenic::ResourceId id,
                               const ui::gfx::EntityNodeArgsPtr& args);
  ResourcePtr CreateShapeNode(scenic::ResourceId id,
                              const ui::gfx::ShapeNodeArgsPtr& args);
  ResourcePtr CreateDisplayCompositor(
      scenic::ResourceId id,
      const ui::gfx::DisplayCompositorArgsPtr& args);
  ResourcePtr CreateImagePipeCompositor(
      scenic::ResourceId id,
      const ui::gfx::ImagePipeCompositorArgsPtr& args);
  ResourcePtr CreateLayerStack(scenic::ResourceId id,
                               const ui::gfx::LayerStackArgsPtr& args);
  ResourcePtr CreateLayer(scenic::ResourceId id,
                          const ui::gfx::LayerArgsPtr& args);
  ResourcePtr CreateCircle(scenic::ResourceId id, float initial_radius);
  ResourcePtr CreateRectangle(scenic::ResourceId id, float width, float height);
  ResourcePtr CreateRoundedRectangle(scenic::ResourceId id,
                                     float width,
                                     float height,
                                     float top_left_radius,
                                     float top_right_radius,
                                     float bottom_right_radius,
                                     float bottom_left_radius);
  ResourcePtr CreateMesh(scenic::ResourceId id);
  ResourcePtr CreateMaterial(scenic::ResourceId id);
  ResourcePtr CreateVariable(scenic::ResourceId id,
                             const ui::gfx::VariableArgsPtr& args);

  // Return false and log an error if the value is not of the expected type.
  // NOTE: although failure does not halt execution of the program, it does
  // indicate client error, and will be used by the caller to tear down the
  // Session.
  bool AssertValueIsOfType(const ui::gfx::ValuePtr& value,
                           const ui::gfx::Value::Tag* tags,
                           size_t tag_count);
  template <size_t N>
  bool AssertValueIsOfType(const ui::gfx::ValuePtr& value,
                           const std::array<ui::gfx::Value::Tag, N>& tags) {
    return AssertValueIsOfType(value, tags.data(), N);
  }

  void FlushEvents();

  friend class Resource;
  void IncrementResourceCount() { ++resource_count_; }
  void DecrementResourceCount() { --resource_count_; }

  struct Update {
    uint64_t presentation_time;

    ::f1dl::Array<ui::gfx::CommandPtr> commands;
    std::unique_ptr<escher::FenceSetListener> acquire_fences;
    ::f1dl::Array<zx::event> release_fences;

    // Callback to report when the update has been applied in response to
    // an invocation of |Session.Present()|.
    ui::Session::PresentCallback present_callback;
  };
  bool ApplyUpdate(Update* update);
  std::queue<Update> scheduled_updates_;
  ::f1dl::Array<zx::event> fences_to_release_on_next_update_;

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
  ::f1dl::Array<ui::EventPtr> buffered_events_;

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
