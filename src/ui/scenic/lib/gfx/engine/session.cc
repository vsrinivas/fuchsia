// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/session.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>

#include <memory>
#include <utility>

#include <trace/event.h>

#include "src/ui/lib/escher/hmd/pose_buffer.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/shape/mesh.h"
#include "src/ui/lib/escher/shape/rounded_rect_factory.h"
#include "src/ui/lib/escher/util/type_utils.h"
#include "src/ui/scenic/lib/gfx/engine/frame_scheduler.h"
#include "src/ui/scenic/lib/gfx/engine/gfx_command_applier.h"
#include "src/ui/scenic/lib/gfx/engine/hit_tester.h"
#include "src/ui/scenic/lib/gfx/engine/session_handler.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/gfx/resources/image_pipe.h"
#include "src/ui/scenic/lib/gfx/swapchain/swapchain_factory.h"
#include "src/ui/scenic/lib/gfx/util/time.h"
#include "src/ui/scenic/lib/gfx/util/unwrap.h"
#include "src/ui/scenic/lib/gfx/util/wrap.h"

namespace scenic_impl {
namespace gfx {

namespace {

#define SESSION_TRACE_ID(session_id, count) (((uint64_t)(session_id) << 32) | (count))

}  // anonymous namespace

Session::Session(SessionId id, SessionContext session_context,
                 std::shared_ptr<EventReporter> event_reporter,
                 std::shared_ptr<ErrorReporter> error_reporter,
                 inspect_deprecated::Node inspect_node)
    : id_(id),
      error_reporter_(std::move(error_reporter)),
      event_reporter_(std::move(event_reporter)),
      session_context_(std::move(session_context)),
      resource_context_(
          /* Sessions can be used in integration tests, with and without Vulkan.
             When Vulkan is unavailable, the Escher pointer is null. These
             ternaries protect against null-pointer dispatching for these
             non-Vulkan tests. */
          {session_context_.vk_device,
           session_context_.escher != nullptr ? session_context_.escher->vk_physical_device()
                                              : vk::PhysicalDevice(),
           session_context_.escher != nullptr ? session_context_.escher->device()->dispatch_loader()
                                              : vk::DispatchLoaderDynamic(),
           session_context_.escher != nullptr ? session_context_.escher->device()->caps()
                                              : escher::VulkanDeviceQueues::Caps(),
           session_context_.escher_resource_recycler, session_context_.escher_image_factory}),
      resources_(error_reporter_),
      image_pipe_updater_(std::make_shared<ImagePipeUpdater>(id, session_context_.frame_scheduler)),
      inspect_node_(std::move(inspect_node)),
      weak_factory_(this) {
  FXL_DCHECK(error_reporter_);
  FXL_DCHECK(event_reporter_);

  inspect_resource_count_ = inspect_node_.CreateUIntMetric("resource_count", 0);
  inspect_last_applied_target_presentation_time_ =
      inspect_node_.CreateUIntMetric("last_applied_target_presentation_time", 0);
  inspect_last_applied_requested_presentation_time_ =
      inspect_node_.CreateUIntMetric("last_applied_request_presentation_time", 0);
  inspect_last_requested_presentation_time_ =
      inspect_node_.CreateUIntMetric("last_requested_presentation_time", 0);
}

Session::~Session() {
  resources_.Clear();

  // We assume the channel for the associated gfx::Session is closed by
  // SessionHandler before this point, since |scheduled_updates_| contains
  // pending callbacks to gfx::Session::Present(). If the channel was not closed
  // we would have to invoke those callbacks before destroying them.
  scheduled_updates_ = {};
  fences_to_release_on_next_update_.clear();

  if (resource_count_ != 0) {
    auto exported_count = session_context_.resource_linker->NumExportsForSession(id());
    FXL_CHECK(resource_count_ == 0)
        << "Session::~Session(): Not all resources have been collected. "
           "Exported resources: "
        << exported_count << ", total outstanding resources: " << resource_count_;
  }
}

EventReporter* Session::event_reporter() const { return event_reporter_.get(); }

bool Session::ScheduleUpdateForPresent(zx::time requested_presentation_time,
                                       std::vector<::fuchsia::ui::gfx::Command> commands,
                                       std::vector<zx::event> acquire_fences,
                                       std::vector<zx::event> release_events,
                                       fuchsia::ui::scenic::Session::PresentCallback callback) {
  // TODO(35521) If the client has no Present()s left, kill the session.
  if (++presents_in_flight_ > kMaxPresentsInFlight) {
    error_reporter_->ERROR() << "Present() called with no more presents left. In the future(Bug "
                                "35521) this will terminate the session.";
  }

  // When we call the PresentCallback, decrement the number of presents the client has in flight.
  fuchsia::ui::scenic::Session::PresentCallback new_callback =
      [this, callback = std::move(callback)](fuchsia::images::PresentationInfo info) {
        --presents_in_flight_;
        callback(info);
      };

  return ScheduleUpdateCommon(requested_presentation_time, std::move(commands),
                              std::move(acquire_fences), std::move(release_events),
                              std::move(new_callback));
}

bool Session::ScheduleUpdateForPresent2(zx::time requested_presentation_time,
                                        std::vector<::fuchsia::ui::gfx::Command> commands,
                                        std::vector<zx::event> acquire_fences,
                                        std::vector<zx::event> release_fences,
                                        Present2Info present2_info) {
  zx::time present_received_time = zx::time(async_now(async_get_default_dispatcher()));
  present2_info.SetPresentReceivedTime(present_received_time);

  return ScheduleUpdateCommon(requested_presentation_time, std::move(commands),
                              std::move(acquire_fences), std::move(release_fences),
                              std::move(present2_info));
}

bool Session::ScheduleUpdateCommon(zx::time requested_presentation_time,
                                   std::vector<::fuchsia::ui::gfx::Command> commands,
                                   std::vector<zx::event> acquire_fences,
                                   std::vector<zx::event> release_fences,
                                   std::variant<PresentCallback, Present2Info> presentation_info) {
  TRACE_DURATION("gfx", "Session::ScheduleUpdate", "session_id", id_, "session_debug_name",
                 debug_name_, "requested time", requested_presentation_time.get());

  // Logic verifying client requests presents in-order.
  zx::time last_scheduled_presentation_time = last_applied_update_presentation_time_;
  if (!scheduled_updates_.empty()) {
    last_scheduled_presentation_time =
        std::max(last_scheduled_presentation_time, scheduled_updates_.back().presentation_time);
  }

  if (requested_presentation_time < last_scheduled_presentation_time) {
    error_reporter_->ERROR() << "scenic_impl::gfx::Session: Present called with out-of-order "
                                "presentation time. "
                             << "requested presentation time=" << requested_presentation_time
                             << ", last scheduled presentation time="
                             << last_scheduled_presentation_time << ".";
    return false;
  }

  auto acquire_fence_set = std::make_unique<escher::FenceSetListener>(std::move(acquire_fences));
  acquire_fence_set->WaitReadyAsync([weak = GetWeakPtr(), requested_presentation_time] {
    // This weak pointer will go out of scope if the channel is killed between a call to Present()
    // and the completion of the acquire fences.
    if (weak) {
      FXL_DCHECK(weak->session_context_.frame_scheduler);
      weak->session_context_.frame_scheduler->ScheduleUpdateForSession(requested_presentation_time,
                                                                       weak->id());
    }
  });

  ++scheduled_update_count_;
  TRACE_FLOW_BEGIN("gfx", "scheduled_update", SESSION_TRACE_ID(id_, scheduled_update_count_));

  inspect_last_requested_presentation_time_.Set(requested_presentation_time.get());

  scheduled_updates_.push(Update{requested_presentation_time, std::move(commands),
                                 std::move(acquire_fence_set), std::move(release_fences),
                                 std::move(presentation_info)});

  return true;
}

void Session::SetOnFramePresentedCallback(OnFramePresentedCallback callback) {
  if (auto weak = GetWeakPtr(); weak) {
    FXL_DCHECK(weak->session_context_.frame_scheduler);
    weak->session_context_.frame_scheduler->SetOnFramePresentedCallbackForSession(
        weak->id(), std::move(callback));
  }
}

Session::ApplyUpdateResult Session::ApplyScheduledUpdates(CommandContext* command_context,
                                                          zx::time target_presentation_time,
                                                          zx::time latched_time) {
  ApplyUpdateResult update_results{
      .success = false, .all_fences_ready = true, .needs_render = false};

  // Ensure updates happen on all exit paths; work happens in the destructor.
  ViewTreeUpdateFinalizer view_tree_update_finalizer(this, command_context->scene_graph());

  while (!scheduled_updates_.empty() &&
         scheduled_updates_.front().presentation_time <= target_presentation_time) {
    auto& update = scheduled_updates_.front();

    FXL_DCHECK(last_applied_update_presentation_time_ <= update.presentation_time);

    if (!update.acquire_fences->ready()) {
      TRACE_INSTANT("gfx", "Session missed frame", TRACE_SCOPE_PROCESS, "session_id", id(),
                    "session_debug_name", debug_name_, "target presentation time",
                    target_presentation_time.get(), "session target presentation time",
                    scheduled_updates_.front().presentation_time.get());
      update_results.all_fences_ready = false;
      break;
    }

    ++applied_update_count_;
    TRACE_FLOW_END("gfx", "scheduled_update", SESSION_TRACE_ID(id_, applied_update_count_));

    if (!ApplyUpdate(command_context, std::move(update.commands))) {
      // An error was encountered while applying the update.
      FXL_LOG(WARNING) << "scenic_impl::gfx::Session::ApplyScheduledUpdates(): "
                          "An error was encountered while applying the update. "
                          "Initiating teardown.";
      update_results.success = false;
      scheduled_updates_ = {};
      return update_results;
    }

    for (size_t i = 0; i < fences_to_release_on_next_update_.size(); ++i) {
      session_context_.release_fence_signaller->AddCPUReleaseFence(
          std::move(fences_to_release_on_next_update_.at(i)));
    }
    fences_to_release_on_next_update_ = std::move(update.release_fences);

    last_applied_update_presentation_time_ = update.presentation_time;

    // Collect Present1 callbacks to be returned by |Engine::UpdateSessions()| as part
    // of the |Session::UpdateResults| struct. Or, if it is a Present2, collect the |Present2Info|s.
    if (auto present_callback = std::get_if<PresentCallback>(&update.present_information)) {
      update_results.present1_callbacks.push(std::move(*present_callback));
    } else {
      auto present2_info = std::get_if<Present2Info>(&update.present_information);
      FXL_DCHECK(present2_info);
      present2_info->SetLatchedTime(latched_time);
      update_results.present2_infos.push(std::move(*present2_info));
    }

    update_results.needs_render = true;
    scheduled_updates_.pop();

    // TODO(SCN-1202): gather statistics about how close the actual
    // presentation_time was to the requested time.
    inspect_last_applied_requested_presentation_time_.Set(
        last_applied_update_presentation_time_.get());
    inspect_last_applied_target_presentation_time_.Set(target_presentation_time.get());
    inspect_resource_count_.Set(resource_count_);
  }

  ImagePipeUpdater::ApplyScheduledUpdatesResult image_pipe_update_results =
      image_pipe_updater_->ApplyScheduledUpdates(target_presentation_time,
                                                 session_context_.release_fence_signaller);

  update_results.needs_render =
      update_results.needs_render || image_pipe_update_results.needs_render;
  update_results.image_pipe_callbacks = std::move(image_pipe_update_results.callbacks);
  update_results.success = true;
  return update_results;
}

void Session::EnqueueEvent(::fuchsia::ui::gfx::Event event) {
  event_reporter_->EnqueueEvent(std::move(event));
}

void Session::EnqueueEvent(::fuchsia::ui::input::InputEvent event) {
  event_reporter_->EnqueueEvent(std::move(event));
}

std::vector<fuchsia::scenic::scheduling::PresentationInfo> Session::GetFuturePresentationInfos(
    zx::duration requested_prediction_span) {
  return {session_context_.frame_scheduler->GetFuturePresentationInfos(requested_prediction_span)};
}

bool Session::SetRootView(fxl::WeakPtr<View> view) {
  // Check that the root view ID is being set or being cleared. If there is
  // already a root view, another cannot be set.
  if (root_view_) {
    return false;
  }

  root_view_ = view;
  return true;
}

ViewTreeUpdates& Session::view_tree_updates() { return view_tree_updates_; }

void Session::TrackViewHolder(fxl::WeakPtr<ViewHolder> view_holder) {
  FXL_DCHECK(view_holder) << "precondition";  // Called in ViewHolder constructor.

  const zx_koid_t koid = view_holder->view_holder_koid();
  view_tree_updates_.push_back(ViewTreeNewAttachNode{.koid = koid});
  auto [iter, inserted] =
      tracked_view_holders_.insert({koid, ViewHolderStatus{.view_holder = std::move(view_holder)}});
  FXL_DCHECK(inserted);
}

void Session::UntrackViewHolder(zx_koid_t koid) {
  // Disconnection in view tree handled by DeleteNode operation.
  view_tree_updates_.push_back(ViewTreeDeleteNode{.koid = koid});
  auto erased_count = tracked_view_holders_.erase(koid);
  FXL_DCHECK(erased_count == 1);
}

void Session::UpdateViewHolderConnections() {
  for (auto& kv : tracked_view_holders_) {
    const zx_koid_t koid = kv.first;
    ViewHolderStatus& status = kv.second;
    const std::optional<bool> prev_connected = status.connected_to_session_root;

    // Each ViewHolder may have an independent intra-Session "root" that connects it upwards.
    // E.g., it's legal to have multiple Scene roots connecting to independent compositors.
    zx_koid_t root = ZX_KOID_INVALID;
    // Determine whether each ViewHolder is connected to some root.
    bool now_connected = false;
    FXL_DCHECK(status.view_holder) << "invariant";
    Node* curr = status.view_holder ? status.view_holder->parent() : nullptr;
    while (curr) {
      if (curr->session_id() != id()) {
        break;  // Exited session boundary, quit upwards search.
      }
      if (curr->IsKindOf<ViewNode>() && curr->As<ViewNode>()->GetView()) {
        root = curr->As<ViewNode>()->GetView()->view_ref_koid();
        FXL_DCHECK(root != ZX_KOID_INVALID) << "invariant";
        // TODO(SCN-1249): Enable following check when one-view-per-session is enforced.
        // FXL_DCHECK(root_view_ && root_view_->view_ref_koid() == root)
        //    << "invariant: session's root-view-discovered and root-view-purported must match.";
        now_connected = true;
        break;
      }
      if (curr->IsKindOf<Scene>()) {
        root = curr->As<Scene>()->view_ref_koid();
        FXL_DCHECK(root != ZX_KOID_INVALID) << "invariant";
        now_connected = true;
        break;
      }
      curr = curr->parent();
    }

    // <prev>   <now>   <action>
    // none     true    record connect, report connect (case 1)
    // none     false   record disconnect (case 2)
    // true     true    (nop)
    // true     false   record disconnect, report disconnect (case 3)
    // false    true    record connect, report connect (case 1)
    // false    false   (nop)
    if ((!prev_connected.has_value() && now_connected) ||
        (prev_connected.has_value() && !prev_connected.value() && now_connected)) {
      // Case 1
      status.connected_to_session_root = std::make_optional<bool>(true);
      view_tree_updates_.push_back(ViewTreeConnectToParent{.child = koid, .parent = root});
    } else if (!prev_connected.has_value() && !now_connected) {
      // Case 2
      status.connected_to_session_root = std::make_optional<bool>(false);
    } else if (prev_connected.has_value() && prev_connected.value() && !now_connected) {
      // Case 3
      status.connected_to_session_root = std::make_optional<bool>(false);
      view_tree_updates_.push_back(ViewTreeDisconnectFromParent{.koid = koid});
    }
  }
}

Session::ViewTreeUpdateFinalizer::ViewTreeUpdateFinalizer(Session* session, SceneGraph* scene_graph)
    : session_(session), scene_graph_(scene_graph) {
  FXL_DCHECK(session_);
  FXL_DCHECK(scene_graph_);
}

Session::ViewTreeUpdateFinalizer::~ViewTreeUpdateFinalizer() {
  session_->UpdateViewHolderConnections();
  session_->StageViewTreeUpdates(scene_graph_);
}

void Session::StageViewTreeUpdates(SceneGraph* scene_graph) {
  scene_graph->StageViewTreeUpdates(std::move(view_tree_updates_));
  view_tree_updates_.clear();
}

bool Session::ApplyUpdate(CommandContext* command_context,
                          std::vector<::fuchsia::ui::gfx::Command> commands) {
  TRACE_DURATION("gfx", "Session::ApplyUpdate");
  for (auto& command : commands) {
    if (!ApplyCommand(command_context, std::move(command))) {
      error_reporter_->ERROR() << "scenic_impl::gfx::Session::ApplyCommand() "
                                  "failed to apply Command: "
                               << command;
      return false;
    }
  }
  return true;
  // TODO: acquire_fences and release_fences should be added to a list that is
  // consumed by the FrameScheduler.
}

}  // namespace gfx
}  // namespace scenic_impl
