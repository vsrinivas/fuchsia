// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/session.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>

#include <memory>
#include <utility>

#include <trace/event.h>

#include "garnet/lib/ui/gfx/engine/frame_scheduler.h"
#include "garnet/lib/ui/gfx/engine/gfx_command_applier.h"
#include "garnet/lib/ui/gfx/engine/hit_tester.h"
#include "garnet/lib/ui/gfx/engine/session_handler.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer_stack.h"
#include "garnet/lib/ui/gfx/resources/image_pipe.h"
#include "garnet/lib/ui/gfx/swapchain/swapchain_factory.h"
#include "garnet/lib/ui/gfx/util/time.h"
#include "garnet/lib/ui/gfx/util/unwrap.h"
#include "garnet/lib/ui/gfx/util/wrap.h"
#include "src/ui/lib/escher/hmd/pose_buffer.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/shape/mesh.h"
#include "src/ui/lib/escher/shape/rounded_rect_factory.h"
#include "src/ui/lib/escher/util/type_utils.h"

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

bool Session::ScheduleUpdate(zx::time requested_presentation_time,
                             std::vector<::fuchsia::ui::gfx::Command> commands,
                             std::vector<zx::event> acquire_fences,
                             std::vector<zx::event> release_events,
                             fuchsia::ui::scenic::Session::PresentCallback callback) {
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

  scheduled_updates_.push(Update{requested_presentation_time, std::move(commands),
                                 std::move(acquire_fence_set), std::move(release_events),
                                 std::move(callback)});

  inspect_last_requested_presentation_time_.Set(requested_presentation_time.get());

  return true;
}

Session::ApplyUpdateResult Session::ApplyScheduledUpdates(CommandContext* command_context,
                                                          zx::time target_presentation_time) {
  ApplyUpdateResult update_results{
      .success = false, .needs_render = false, .all_fences_ready = true};

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
    // Collect callbacks to be returned by |Engine::UpdateSessions()| as part
    // of the |Session::UpdateResults| struct.
    update_results.callbacks.push(std::move(update.present_callback));
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
      image_pipe_updater_->ApplyScheduledUpdates(command_context, target_presentation_time,
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
  // TODO: acquire_fences and release_fences should be added to a list that is
  // consumed by the FrameScheduler.
}

}  // namespace gfx
}  // namespace scenic_impl
