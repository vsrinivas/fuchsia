// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/session.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>
#include <trace/event.h>

#include <memory>
#include <utility>

#include "garnet/lib/ui/gfx/engine/gfx_command_applier.h"
#include "garnet/lib/ui/gfx/engine/hit_tester.h"
#include "garnet/lib/ui/gfx/engine/session_handler.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer_stack.h"
#include "garnet/lib/ui/gfx/resources/image_pipe.h"
#include "garnet/lib/ui/gfx/swapchain/swapchain_factory.h"
#include "garnet/lib/ui/gfx/util/time.h"
#include "garnet/lib/ui/gfx/util/unwrap.h"
#include "garnet/lib/ui/gfx/util/wrap.h"
#include "lib/escher/hmd/pose_buffer.h"
#include "lib/escher/renderer/batch_gpu_uploader.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/shape/rounded_rect_factory.h"
#include "lib/escher/util/type_utils.h"

namespace scenic_impl {
namespace gfx {

namespace {

#define SESSION_TRACE_ID(session_id, count) \
  (((uint64_t)(session_id) << 32) | (count))

// Converts the provided vector of Hits into a fidl array of HitPtrs.
fidl::VectorPtr<::fuchsia::ui::gfx::Hit> WrapHits(
    const std::vector<Hit>& hits) {
  fidl::VectorPtr<::fuchsia::ui::gfx::Hit> wrapped_hits;
  wrapped_hits.resize(hits.size());
  for (size_t i = 0; i < hits.size(); ++i) {
    const Hit& hit = hits[i];
    fuchsia::ui::gfx::Hit wrapped_hit;
    wrapped_hit.tag_value = hit.tag_value;
    wrapped_hit.ray_origin = Wrap(hit.ray.origin);
    wrapped_hit.ray_direction = Wrap(hit.ray.direction);
    wrapped_hit.inverse_transform = Wrap(hit.inverse_transform);
    wrapped_hit.distance = hit.distance;
    wrapped_hits->at(i) = std::move(wrapped_hit);
  }
  return wrapped_hits;
}
}  // anonymous namespace

Session::Session(SessionId id, SessionContext session_context,
                 EventReporter* event_reporter, ErrorReporter* error_reporter)
    : id_(id),
      error_reporter_(error_reporter),
      event_reporter_(event_reporter),
      session_context_(std::move(session_context)),
      resource_context_({session_context_.vk_device,
                         session_context_.escher != nullptr
                             ? session_context_.escher->device()->caps()
                             : escher::VulkanDeviceQueues::Caps(),
                         session_context_.imported_memory_type_index,
                         session_context_.escher_resource_recycler,
                         session_context_.escher_image_factory}),
      resources_(error_reporter),
      weak_factory_(this) {
  FXL_DCHECK(error_reporter);
}

Session::~Session() {
  resources_.Clear();
  scheduled_image_pipe_updates_ = {};

  // We assume the channel for the associated gfx::Session is closed by
  // SessionHandler before this point, since |scheduled_updates_| contains
  // pending callbacks to gfx::Session::Present(). If the channel was not closed
  // we would have to invoke those callbacks before destroying them.
  scheduled_updates_ = {};
  fences_to_release_on_next_update_.clear();

  if (resource_count_ != 0) {
    auto exported_count =
        session_context_.resource_linker->NumExportsForSession(this);
    FXL_CHECK(resource_count_ == 0)
        << "Session::~Session(): Not all resources have been collected. "
           "Exported resources: "
        << exported_count
        << ", total outstanding resources: " << resource_count_;
  }
  error_reporter_ = nullptr;
}

ErrorReporter* Session::error_reporter() const {
  return error_reporter_ ? error_reporter_ : ErrorReporter::Default();
}

EventReporter* Session::event_reporter() const { return event_reporter_; }

bool Session::ScheduleUpdate(
    uint64_t requested_presentation_time,
    std::vector<::fuchsia::ui::gfx::Command> commands,
    std::vector<zx::event> acquire_fences,
    std::vector<zx::event> release_events,
    fuchsia::ui::scenic::Session::PresentCallback callback) {
  TRACE_DURATION("gfx", "Session::ScheduleUpdate", "session_id", id_,
                 "session_debug_name", debug_name_, "requested time",
                 requested_presentation_time);

  // Logic verifying client requests presents in-order.
  uint64_t last_scheduled_presentation_time =
      last_applied_update_presentation_time_;
  if (!scheduled_updates_.empty()) {
    last_scheduled_presentation_time =
        std::max(last_scheduled_presentation_time,
                 scheduled_updates_.back().presentation_time);
  }

  if (requested_presentation_time < last_scheduled_presentation_time) {
    error_reporter_->ERROR()
        << "scenic_impl::gfx::Session: Present called with out-of-order "
           "presentation time. "
        << "requested presentation time=" << requested_presentation_time
        << ", last scheduled presentation time="
        << last_scheduled_presentation_time << ".";
    return false;
  }

  auto acquire_fence_set =
      std::make_unique<escher::FenceSetListener>(std::move(acquire_fences));
  acquire_fence_set->WaitReadyAsync(
      [weak = GetWeakPtr(), requested_presentation_time] {
        if (weak) {
          weak->session_context_.frame_scheduler->ScheduleUpdateForSession(
              requested_presentation_time, weak->id());
        }
      });

  ++scheduled_update_count_;
  TRACE_FLOW_BEGIN("gfx", "scheduled_update",
                   SESSION_TRACE_ID(id_, scheduled_update_count_));

  scheduled_updates_.push(
      Update{requested_presentation_time, std::move(commands),
             std::move(acquire_fence_set), std::move(release_events),
             std::move(callback)});

  return true;
}

void Session::ScheduleImagePipeUpdate(uint64_t presentation_time,
                                      ImagePipePtr image_pipe) {
  FXL_DCHECK(image_pipe);
  scheduled_image_pipe_updates_.push(
      {presentation_time, std::move(image_pipe)});

  session_context_.frame_scheduler->ScheduleUpdateForSession(presentation_time,
                                                             id_);
}

Session::ApplyUpdateResult Session::ApplyScheduledUpdates(
    CommandContext* command_context, uint64_t target_presentation_time,
    uint64_t needs_render_id) {
  FXL_DCHECK(target_presentation_time >= last_presentation_time_);
  TRACE_DURATION("gfx", "Session::ApplyScheduledUpdates", "session_id", id_,
                 "session_debug_name", debug_name_, "target_presentation_time",
                 target_presentation_time);

  ApplyUpdateResult update_results{
      .success = false, .needs_render = false, .all_fences_ready = true};

  while (!scheduled_updates_.empty() &&
         scheduled_updates_.front().presentation_time <
             target_presentation_time) {
    auto& update = scheduled_updates_.front();
    FXL_DCHECK(last_applied_update_presentation_time_ <=
               update.presentation_time);

    if (!update.acquire_fences->ready()) {
      TRACE_INSTANT("gfx", "Session missed frame", TRACE_SCOPE_PROCESS,
                    "session_id", id(), "session_debug_name", debug_name_,
                    "target presentation time", target_presentation_time,
                    "session target presentation time",
                    scheduled_updates_.front().presentation_time);
      update_results.all_fences_ready = false;
      break;
    }

    ++applied_update_count_;
    TRACE_FLOW_END("gfx", "scheduled_update",
                   SESSION_TRACE_ID(id_, applied_update_count_));

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
    // Collect callbacks to be signalled in
    // |Engine::SignalSuccessfulPresentCallbacks|
    update_results.callbacks.push(std::move(update.present_callback));
    update_results.needs_render = true;
    scheduled_updates_.pop();

    // TODO(SCN-1202): gather statistics about how close the actual
    // presentation_time was to the requested time.
  }

  // TODO(SCN-1219): Unify with other session updates.
  std::unordered_map<ResourceId, ImagePipePtr> image_pipe_updates_to_upload;
  while (!scheduled_image_pipe_updates_.empty() &&
         scheduled_image_pipe_updates_.top().presentation_time <=
             target_presentation_time) {
    auto& update = scheduled_image_pipe_updates_.top();
    if (update.image_pipe) {
      auto image_pipe_update_results = update.image_pipe->Update(
          session_context_.release_fence_signaller, target_presentation_time);

      // Collect callbacks to be signalled in
      // |Engine::SignalSuccessfulPresentCallbacks|
      while (!image_pipe_update_results.callbacks.empty()) {
        update_results.image_pipe_callbacks.push(
            std::move(image_pipe_update_results.callbacks.front()));
        image_pipe_update_results.callbacks.pop();
      }

      // Only upload images that were updated and are currently dirty, and only
      // do one upload per ImagePipe.
      if (image_pipe_update_results.image_updated) {
        image_pipe_updates_to_upload.try_emplace(update.image_pipe->id(),
                                                 std::move(update.image_pipe));
      }
    }
    scheduled_image_pipe_updates_.pop();
  }

  // Stage GPU uploads for the latest dirty image on each updated ImagePipe.
  for (const auto& entry : image_pipe_updates_to_upload) {
    ImagePipePtr image_pipe = entry.second;
    image_pipe->UpdateEscherImage(command_context->batch_gpu_uploader());
    // Image was updated so the image in the scene is dirty.
    update_results.needs_render = true;
  }
  image_pipe_updates_to_upload.clear();

  if (update_results.needs_render) {
    TRACE_FLOW_BEGIN("gfx", "needs_render", needs_render_id);
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
  // TODO: acquire_fences and release_fences should be added to a list that is
  // consumed by the FrameScheduler.
}

void Session::HitTest(uint32_t node_id, fuchsia::ui::gfx::vec3 ray_origin,
                      fuchsia::ui::gfx::vec3 ray_direction,
                      fuchsia::ui::scenic::Session::HitTestCallback callback) {
  if (auto node = resources_.FindResource<Node>(node_id)) {
    SessionHitTester hit_tester(node->session());
    std::vector<Hit> hits = hit_tester.HitTest(
        node.get(), escher::ray4{escher::vec4(Unwrap(ray_origin), 1.f),
                                 escher::vec4(Unwrap(ray_direction), 0.f)});
    callback(WrapHits(hits));
  } else {
    // TODO(SCN-162): Currently the test fails if the node isn't presented yet.
    // Perhaps we should given clients more control over which state of
    // the scene graph will be consulted for hit testing purposes.
    error_reporter_->WARN()
        << "Cannot perform hit test because node " << node_id
        << " does not exist in the currently presented content.";
    callback(nullptr);
  }
}

void Session::HitTestDeviceRay(
    fuchsia::ui::gfx::vec3 ray_origin, fuchsia::ui::gfx::vec3 ray_direction,
    fuchsia::ui::scenic::Session::HitTestCallback callback) {
  escher::ray4 ray =
      escher::ray4{{Unwrap(ray_origin), 1.f}, {Unwrap(ray_direction), 0.f}};

  // The layer stack expects the input to the hit test to be in unscaled device
  // coordinates.
  SessionHitTester hit_tester(this);
  // TODO(SCN-1170): get rid of SceneGraph::first_compositor().
  std::vector<Hit> layer_stack_hits =
      session_context_.scene_graph->first_compositor()->layer_stack()->HitTest(
          ray, &hit_tester);

  callback(WrapHits(layer_stack_hits));
}

}  // namespace gfx
}  // namespace scenic_impl
