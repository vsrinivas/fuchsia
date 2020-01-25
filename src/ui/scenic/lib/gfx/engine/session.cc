// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/session.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>

#include <memory>
#include <utility>

#include <fbl/auto_call.h>
#include <trace/event.h>

#include "src/ui/lib/escher/hmd/pose_buffer.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/shape/mesh.h"
#include "src/ui/lib/escher/shape/rounded_rect_factory.h"
#include "src/ui/lib/escher/util/type_utils.h"
#include "src/ui/scenic/lib/gfx/engine/gfx_command_applier.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/gfx/swapchain/swapchain_factory.h"
#include "src/ui/scenic/lib/gfx/util/time.h"
#include "src/ui/scenic/lib/gfx/util/unwrap.h"
#include "src/ui/scenic/lib/gfx/util/wrap.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"

using scheduling::Present2Info;

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
      view_tree_updater_(id),
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

  // We assume the channel for the associated gfx::Session is closed before this point,
  // since |scheduled_updates_| contains pending callbacks to gfx::Session::Present().
  // If the channel was not closed we would have to invoke those callbacks before destroying them.
  scheduled_updates_ = {};
  fences_to_release_on_next_update_.clear();

  FXL_CHECK(resource_count_ == 0) << "Session::~Session(): " << resource_count_
                                  << " resources have not yet been destroyed.";
}

void Session::DispatchCommand(fuchsia::ui::scenic::Command command) {
  FXL_DCHECK(command.Which() == fuchsia::ui::scenic::Command::Tag::kGfx);
  buffered_commands_.emplace_back(std::move(command.gfx()));
}

EventReporter* Session::event_reporter() const { return event_reporter_.get(); }

bool Session::ScheduleUpdateForPresent(zx::time requested_presentation_time,
                                       std::vector<zx::event> release_events,
                                       fuchsia::ui::scenic::Session::PresentCallback callback) {
  // TODO(35521) If the client has no Present()s left, kill the session.
  if (++presents_in_flight_ > scheduling::FrameScheduler::kMaxPresentsInFlight) {
    error_reporter_->ERROR() << "Present() called with no more presents left. In the future(Bug "
                                "35521) this will terminate the session.";
  }

  // When we call the PresentCallback, decrement the number of presents the client has in flight.
  fuchsia::ui::scenic::Session::PresentCallback new_callback =
      [this, callback = std::move(callback)](fuchsia::images::PresentationInfo info) {
        --presents_in_flight_;
        callback(info);
      };

  return ScheduleUpdateCommon(requested_presentation_time, std::move(release_events),
                              std::move(new_callback));
}

bool Session::ScheduleUpdateForPresent2(zx::time requested_presentation_time,
                                        std::vector<zx::event> release_fences,
                                        Present2Info present2_info) {
  zx::time present_received_time = zx::time(async_now(async_get_default_dispatcher()));
  present2_info.SetPresentReceivedTime(present_received_time);

  return ScheduleUpdateCommon(requested_presentation_time, std::move(release_fences),
                              std::move(present2_info));
}

bool Session::ScheduleUpdateCommon(zx::time requested_presentation_time,
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

  async::PostTask(async_get_default_dispatcher(),
                  [weak = GetWeakPtr(), requested_presentation_time] {
                    // This weak pointer will go out of scope if the channel is killed between a
                    // call to Present() and the looper executing this task.
                    if (weak) {
                      FXL_DCHECK(weak->session_context_.frame_scheduler);
                      weak->session_context_.frame_scheduler->ScheduleUpdateForSession(
                          requested_presentation_time, weak->id());
                    }
                  });

  ++scheduled_update_count_;
  TRACE_FLOW_BEGIN("gfx", "scheduled_update", SESSION_TRACE_ID(id_, scheduled_update_count_));

  inspect_last_requested_presentation_time_.Set(requested_presentation_time.get());

  scheduled_updates_.push(Update{requested_presentation_time, std::move(buffered_commands_),
                                 std::move(release_fences), std::move(presentation_info)});
  buffered_commands_.clear();

  return true;
}

Session::ApplyUpdateResult Session::ApplyScheduledUpdates(CommandContext* command_context,
                                                          zx::time target_presentation_time,
                                                          zx::time latched_time) {
  ApplyUpdateResult update_results{
      .success = false, .all_fences_ready = true, .needs_render = false};

  // RAII object to ensure UpdateViewHolderConnections and StageViewTreeUpdates, on all exit paths.
  fbl::AutoCall cleanup([this, command_context] {
    view_tree_updater_.UpdateViewHolderConnections();
    view_tree_updater_.StageViewTreeUpdates(command_context->scene_graph());
  });

  while (!scheduled_updates_.empty() &&
         scheduled_updates_.front().presentation_time <= target_presentation_time) {
    auto& update = scheduled_updates_.front();

    FXL_DCHECK(last_applied_update_presentation_time_ <= update.presentation_time);

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
      update_results.present1_callbacks.push_back(std::move(*present_callback));
    } else {
      auto present2_info = std::get_if<Present2Info>(&update.present_information);
      FXL_DCHECK(present2_info);
      present2_info->SetLatchedTime(latched_time);
      update_results.present2_infos.push_back(std::move(*present2_info));
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

  update_results.success = true;
  return update_results;
}

void Session::EnqueueEvent(::fuchsia::ui::gfx::Event event) {
  event_reporter_->EnqueueEvent(std::move(event));
}

void Session::EnqueueEvent(::fuchsia::ui::input::InputEvent event) {
  event_reporter_->EnqueueEvent(std::move(event));
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
}

}  // namespace gfx
}  // namespace scenic_impl
