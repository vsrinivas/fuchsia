// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_SESSION_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_SESSION_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>

#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/inspect_deprecated/inspect.h"
#include "src/ui/lib/escher/flib/fence_set_listener.h"
#include "src/ui/scenic/lib/gfx/engine/gfx_command_applier.h"
#include "src/ui/scenic/lib/gfx/engine/image_pipe_updater.h"
#include "src/ui/scenic/lib/gfx/engine/resource_map.h"
#include "src/ui/scenic/lib/gfx/engine/session_context.h"
#include "src/ui/scenic/lib/gfx/engine/session_manager.h"
#include "src/ui/scenic/lib/gfx/engine/view_tree.h"
#include "src/ui/scenic/lib/gfx/resources/memory.h"
#include "src/ui/scenic/lib/gfx/resources/resource_context.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"
#include "src/ui/scenic/lib/scenic/present2_info.h"
#include "src/ui/scenic/lib/scenic/session.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"
#include "src/ui/scenic/lib/scheduling/id.h"

namespace scenic_impl {
namespace gfx {

using PresentCallback = fuchsia::ui::scenic::Session::PresentCallback;
using OnFramePresentedCallback =
    fit::function<void(fuchsia::scenic::scheduling::FramePresentedInfo info)>;

class CommandContext;
class Resource;

// gfx::Session is the internal endpoint of the scenic::Session channel.
// It owns, and is responsible for, all graphics state on the channel
class Session {
 public:
  // Return type for ApplyScheduledUpdate
  struct ApplyUpdateResult {
    bool success;
    bool all_fences_ready;
    bool needs_render;

    std::deque<PresentCallback> present1_callbacks;
    std::deque<Present2Info> present2_infos;
    std::deque<PresentImageCallback> image_pipe_callbacks;
  };

  Session(SessionId id, SessionContext context,
          std::shared_ptr<EventReporter> event_reporter = EventReporter::Default(),
          std::shared_ptr<ErrorReporter> error_reporter = ErrorReporter::Default(),
          inspect_deprecated::Node inspect_node = inspect_deprecated::Node());
  virtual ~Session();

  // Apply the operation to the current session state.  Return true if
  // successful, and false if the op is somehow invalid.  In the latter case,
  // the Session is left unchanged.
  bool ApplyCommand(CommandContext* command_context, fuchsia::ui::gfx::Command command) {
    return GfxCommandApplier::ApplyCommand(this, command_context, std::move(command));
  }

  SessionId id() const { return id_; }

  const fxl::WeakPtr<Session> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }
  const SessionContext& session_context() const { return session_context_; }
  const ResourceContext& resource_context() const { return resource_context_; }

  const std::shared_ptr<ImagePipeUpdater>& image_pipe_updater() const {
    return image_pipe_updater_;
  }

  // Return the total number of existing resources associated with this Session.
  size_t GetTotalResourceCount() const { return resource_count_; }

  // Return the number of resources that a client can identify via a
  // ResourceId. This number is decremented when a
  // ReleaseResourceCmd is applied.  However, the resource may continue to
  // exist if it is referenced by other resources.
  size_t GetMappedResourceCount() const { return resources_.size(); }

  std::shared_ptr<ErrorReporter> shared_error_reporter() const { return error_reporter_; }
  ErrorReporter* error_reporter() const { return error_reporter_.get(); }
  EventReporter* event_reporter() const;  // Never nullptr.

  ResourceMap* resources() { return &resources_; }
  const ResourceMap* resources() const { return &resources_; }

  // Called by SessionHandler::Present().  Stashes the arguments without applying them; they will
  // later be applied by ApplyScheduledUpdates().
  bool ScheduleUpdateForPresent(zx::time presentation_time,
                                std::vector<::fuchsia::ui::gfx::Command> commands,
                                std::vector<zx::event> acquire_fences,
                                std::vector<zx::event> release_fences, PresentCallback callback);

  // Called by SessionHandler::Present2(). Like Present(), it stashes the arguments without
  // applying them; they will later be applied by ApplyScheduledUpdates().
  bool ScheduleUpdateForPresent2(zx::time requested_presentation_time,
                                 std::vector<::fuchsia::ui::gfx::Command> commands,
                                 std::vector<zx::event> acquire_fences,
                                 std::vector<zx::event> release_fences, Present2Info present2_info);

  // Called by Engine() when it is notified by the FrameScheduler that a frame should be rendered
  // for the specified |actual_presentation_time|. Returns ApplyUpdateResult.success as true if
  // updates were successfully applied, false if updates failed to be applied. Returns
  // ApplyUpdateResult.needs_render as true if any changes were applied, false if none were.
  //
  // |target_presentation_time| is the time the session specified it would like to be scheduled
  // for, and is used for tracing.
  // |presentation_interval| is the estimated time until next frame and is returned to the client.
  // |latched_time| is the deadline such that all updates submitted prior to it were grouped
  // together.
  ApplyUpdateResult ApplyScheduledUpdates(CommandContext* command_context,
                                          zx::time target_presentation_time, zx::time latched_time);

  // Sets the |fuchsia::ui::scenic::Session::OnFramePresented| event handler. This should only be
  // called once per session.
  void SetOnFramePresentedCallback(OnFramePresentedCallback callback);

  // Convenience.  Forwards an event to the EventReporter.
  void EnqueueEvent(::fuchsia::ui::gfx::Event event);
  void EnqueueEvent(::fuchsia::ui::input::InputEvent event);

  // Returns information about future presentation times, and their respective latch points.
  //
  // See fuchsia::ui::scenic::RequestPresentationTimes for more details.
  std::vector<fuchsia::scenic::scheduling::PresentationInfo> GetFuturePresentationInfos(
      zx::duration requested_prediction_span);

  void SetDebugName(const std::string& debug_name) { debug_name_ = debug_name; }

  // Clients cannot call Present() anymore when |presents_in_flight_| reaches this value. Scenic
  // uses this to apply backpressure to clients.
  static constexpr int64_t kMaxPresentsInFlight = ::scenic_impl::Session::kMaxPresentsInFlight;
  int64_t presents_in_flight() { return presents_in_flight_; }

 private:
  friend class Resource;
  void IncrementResourceCount() { ++resource_count_; }
  void DecrementResourceCount() { --resource_count_; }

  friend class GfxCommandApplier;
  bool SetRootView(fxl::WeakPtr<View> view);

  friend class View;
  friend class Scene;
  ViewTreeUpdates& view_tree_updates();

  friend class ViewHolder;
  void TrackViewHolder(fxl::WeakPtr<ViewHolder> view_holder);
  void UntrackViewHolder(zx_koid_t koid);
  void UpdateViewHolderConnections();

  // RAII object to ensure UpdateViewHolderConnections and StageViewTreeUpdates, on all exit paths.
  class ViewTreeUpdateFinalizer final {
   public:
    ViewTreeUpdateFinalizer(Session* session, SceneGraph* scene_graph);
    ~ViewTreeUpdateFinalizer();

   private:
    Session* const session_ = nullptr;
    SceneGraph* const scene_graph_ = nullptr;
  };

  // Notify SceneGraph about accumulated ViewHolder/ViewRef updates, but do not apply them yet.
  void StageViewTreeUpdates(SceneGraph* scene_graph);

  bool ScheduleUpdateCommon(zx::time requested_presentation_time,
                            std::vector<::fuchsia::ui::gfx::Command> commands,
                            std::vector<zx::event> acquire_fences,
                            std::vector<zx::event> release_fences,
                            std::variant<PresentCallback, Present2Info> presentation_info);

  struct Update {
    zx::time presentation_time;

    std::vector<::fuchsia::ui::gfx::Command> commands;
    std::unique_ptr<escher::FenceSetListener> acquire_fences;
    std::vector<zx::event> release_fences;

    // Holds either Present1's |fuchsia::ui::scenic::PresentCallback| or Present2's |Present2Info|.
    std::variant<PresentCallback, Present2Info> present_information;
  };

  bool ApplyUpdate(CommandContext* command_context,
                   std::vector<::fuchsia::ui::gfx::Command> commands);
  std::queue<Update> scheduled_updates_;
  std::vector<zx::event> fences_to_release_on_next_update_;

  zx::time last_applied_update_presentation_time_ = zx::time(0);

  const SessionId id_;
  std::string debug_name_;

  const std::shared_ptr<ErrorReporter> error_reporter_;
  const std::shared_ptr<EventReporter> event_reporter_;

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

  struct ViewHolderStatus {
    fxl::WeakPtr<ViewHolder> view_holder;
    // Three cases:
    // - std::nullopt: connectivity unknown
    // - true: connected to session's root (either a View or a Scene).
    // - false: not connected to session's root.
    std::optional<bool> connected_to_session_root;
  };

  // Map of Session's "live" ViewHolder objects that tracks "session root" connectivity.
  std::unordered_map<zx_koid_t, ViewHolderStatus> tracked_view_holders_;

  // Sequentially ordered updates for ViewRef and ViewHolder objects in this Session.
  // Actively maintained over a session update.
  ViewTreeUpdates view_tree_updates_;

  // Tracks the number of method calls for tracing.
  uint64_t scheduled_update_count_ = 0;
  uint64_t applied_update_count_ = 0;

  std::shared_ptr<ImagePipeUpdater> image_pipe_updater_;

  // Combined with |kMaxFramesInFlight|, track how many Present()s the client can still call. We
  // use this for throttling clients.
  //
  // It is incremented on every Present(), and decremented on every OnPresentedCallback().
  int64_t presents_in_flight_ = 0;

  inspect_deprecated::Node inspect_node_;
  inspect_deprecated::UIntMetric inspect_resource_count_;
  inspect_deprecated::UIntMetric inspect_last_applied_target_presentation_time_;
  inspect_deprecated::UIntMetric inspect_last_applied_requested_presentation_time_;
  inspect_deprecated::UIntMetric inspect_last_requested_presentation_time_;

  fxl::WeakPtrFactory<Session> weak_factory_;  // must be last
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_SESSION_H_
