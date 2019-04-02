// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_SESSION_H_
#define GARNET_LIB_UI_GFX_ENGINE_SESSION_H_

#include <vector>

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>

#include "garnet/lib/ui/gfx/engine/gfx_command_applier.h"
#include "garnet/lib/ui/gfx/engine/resource_map.h"
#include "garnet/lib/ui/gfx/engine/session_context.h"
#include "garnet/lib/ui/gfx/engine/session_manager.h"
#include "garnet/lib/ui/gfx/id.h"
#include "garnet/lib/ui/gfx/resources/memory.h"
#include "garnet/lib/ui/gfx/resources/resource_context.h"
#include "garnet/lib/ui/scenic/event_reporter.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "lib/escher/flib/fence_set_listener.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace scenic_impl {
namespace gfx {

class ImagePipe;
using ImagePipePtr = fxl::RefPtr<ImagePipe>;

class CommandContext;
class Engine;
class Resource;

// gfx::Session is the internal endpoint of the scenic::Session channel.
// It owns, and is responsible for, all graphics state on the channel
class Session {
 public:
  // Return type for ApplyScheduledUpdate
  struct ApplyUpdateResult {
    bool success;
    bool needs_render;
  };

  Session(SessionId id, SessionContext context,
          EventReporter* event_reporter = EventReporter::Default(),
          ErrorReporter* error_reporter = ErrorReporter::Default());
  virtual ~Session();

  // Apply the operation to the current session state.  Return true if
  // successful, and false if the op is somehow invalid.  In the latter case,
  // the Session is left unchanged.
  bool ApplyCommand(CommandContext* command_context,
                    fuchsia::ui::gfx::Command command) {
    return GfxCommandApplier::ApplyCommand(this, command_context,
                                           std::move(command));
  }

  SessionId id() const { return id_; }

  const fxl::WeakPtr<Session> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }
  const SessionContext& session_context() const { return session_context_; }
  const ResourceContext& resource_context() const { return resource_context_; }

  // Return the total number of existing resources associated with this Session.
  size_t GetTotalResourceCount() const { return resource_count_; }

  // Return the number of resources that a client can identify via a
  // ResourceId. This number is decremented when a
  // ReleaseResourceCmd is applied.  However, the resource may continue to
  // exist if it is referenced by other resources.
  size_t GetMappedResourceCount() const { return resources_.size(); }

  ErrorReporter* error_reporter() const;  // Never nullptr.
  EventReporter* event_reporter() const;  // Never nullptr.

  ResourceMap* resources() { return &resources_; }

  // Called by SessionHandler::Present().  Stashes the arguments without
  // applying them; they will later be applied by ApplyScheduledUpdates().
  bool ScheduleUpdate(uint64_t presentation_time,
                      std::vector<::fuchsia::ui::gfx::Command> commands,
                      std::vector<zx::event> acquire_fences,
                      std::vector<zx::event> release_fences,
                      fuchsia::ui::scenic::Session::PresentCallback callback);

  // Called by ImagePipe::PresentImage().  Stashes the arguments without
  // applying them; they will later be applied by ApplyScheduledUpdates().
  void ScheduleImagePipeUpdate(uint64_t presentation_time,
                               ImagePipePtr image_pipe);

  // Called by Engine() when it is notified by the FrameScheduler that
  // a frame should be rendered for the specified |actual_presentation_time|.
  // Returns ApplyUpdateResult.success as true if updates were successfully
  // applied, false if updates failed to be applied.
  // Returns ApplyUpdateResult.needs_render as true if any changes were
  // applied, false if none were.
  // |requested_presentation_time| is the time the session specified it would
  // like to be scheduled for, and is used for tracing.
  // |presentation_interval| is the estimated time until next frame and is
  // returned to the client.
  // |needs_render_id| is the id given for starting a trace flow that hooks to
  // RenderFrame event for this Session if it is setting
  // ApplyUpdateResult::needs_render.
  ApplyUpdateResult ApplyScheduledUpdates(CommandContext* command_context,
                                          uint64_t requested_presentation_time,
                                          uint64_t actual_presentation_time,
                                          uint64_t presentation_interval,
                                          uint64_t needs_render_id);

  // Convenience.  Forwards an event to the EventReporter.
  void EnqueueEvent(::fuchsia::ui::gfx::Event event);
  void EnqueueEvent(::fuchsia::ui::input::InputEvent event);

  // Called by SessionHandler::HitTest().
  void HitTest(uint32_t node_id, fuchsia::ui::gfx::vec3 ray_origin,
               fuchsia::ui::gfx::vec3 ray_direction,
               fuchsia::ui::scenic::Session::HitTestCallback callback);

  // Called by SessionHandler::HitTestDeviceRay().
  void HitTestDeviceRay(::fuchsia::ui::gfx::vec3 ray_origin,
                        fuchsia::ui::gfx::vec3 ray_direction,
                        fuchsia::ui::scenic::Session::HitTestCallback callback);

  void SetDebugName(const std::string& debug_name) { debug_name_ = debug_name; }

 private:
  friend class Resource;
  void IncrementResourceCount() { ++resource_count_; }
  void DecrementResourceCount() { --resource_count_; }

  friend class GfxCommandApplier;
  bool SetRootView(fxl::WeakPtr<View> view);

  struct Update {
    uint64_t presentation_time;

    std::vector<::fuchsia::ui::gfx::Command> commands;
    std::unique_ptr<escher::FenceSetListener> acquire_fences;
    std::vector<zx::event> release_fences;

    // Callback to report when the update has been applied in response to
    // an invocation of |Session.Present()|.
    fuchsia::ui::scenic::Session::PresentCallback present_callback;
  };
  bool ApplyUpdate(CommandContext* command_context,
                   std::vector<::fuchsia::ui::gfx::Command> commands);
  std::queue<Update> scheduled_updates_;
  std::vector<zx::event> fences_to_release_on_next_update_;

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
  std::string debug_name_;
  ErrorReporter* error_reporter_ = nullptr;
  EventReporter* event_reporter_ = nullptr;

  // Context objects should be above ResourceMap so they are destroyed after
  // Resources; their lifecycle must exceed that of the Resources.
  SessionContext session_context_;
  ResourceContext resource_context_;
  ResourceMap resources_;
  // The total number of live resources in this session, i.e. resources
  // which have been created and not yet destroyed. Note: there may be resources
  // alive that are not part of |resources_|, such as a Node that is referenced
  // by a parent Node in the scene graph.
  size_t resource_count_ = 0;
  // A weak reference to the first View added to the ResourceMap. Cached while
  // transitioning to a one-root-view-per-session model. See SCN-1249 for more
  // details.
  fxl::WeakPtr<View> root_view_;

  // Tracks the number of method calls for tracing.
  uint64_t scheduled_update_count_ = 0;
  uint64_t applied_update_count_ = 0;

  fxl::WeakPtrFactory<Session> weak_factory_;  // must be last
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_SESSION_H_
