// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/flatland_display.h"

#include <lib/async/default.h>
#include <lib/ui/scenic/cpp/view_identity.h>

#include "src/ui/scenic/lib/utils/logging.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>

static void ReportError() {
  // TODO(fxbug.dev/77035): investigate how to propagate errors back to clients.
  // TODO(fxbug.dev/76640): OK to crash until we have error propagation?  Probably so: better that
  // clients get feedback that they've done something wrong.  These are all in-tree clients, anyway.
  FX_CHECK(false) << "Crashing on error.";
}

using fuchsia::ui::composition::ChildViewWatcher;
using fuchsia::ui::composition::ViewportProperties;
using fuchsia::ui::views::ViewportCreationToken;

namespace flatland {

std::shared_ptr<FlatlandDisplay> FlatlandDisplay::New(
    std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
    fidl::InterfaceRequest<fuchsia::ui::composition::FlatlandDisplay> request,
    scheduling::SessionId session_id, std::shared_ptr<scenic_impl::display::Display> display,
    std::function<void()> destroy_display_function,
    std::shared_ptr<FlatlandPresenter> flatland_presenter, std::shared_ptr<LinkSystem> link_system,
    std::shared_ptr<UberStructSystem::UberStructQueue> uber_struct_queue) {
  return std::shared_ptr<FlatlandDisplay>(
      new FlatlandDisplay(std::move(dispatcher_holder), std::move(request), session_id, display,
                          std::move(destroy_display_function), std::move(flatland_presenter),
                          std::move(link_system), std::move(uber_struct_queue)));
}

FlatlandDisplay::FlatlandDisplay(
    std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
    fidl::InterfaceRequest<fuchsia::ui::composition::FlatlandDisplay> request,
    scheduling::SessionId session_id, std::shared_ptr<scenic_impl::display::Display> display,
    std::function<void()> destroy_display_function,
    std::shared_ptr<FlatlandPresenter> flatland_presenter, std::shared_ptr<LinkSystem> link_system,
    std::shared_ptr<UberStructSystem::UberStructQueue> uber_struct_queue)
    : dispatcher_holder_(std::move(dispatcher_holder)),
      binding_(this, std::move(request), dispatcher_holder_->dispatcher()),
      session_id_(session_id),
      display_(std::move(display)),
      destroy_display_function_(std::move(destroy_display_function)),
      peer_closed_waiter_(binding_.channel().get(), ZX_CHANNEL_PEER_CLOSED),
      flatland_presenter_(std::move(flatland_presenter)),
      link_system_(std::move(link_system)),
      uber_struct_queue_(std::move(uber_struct_queue)),
      transform_graph_(session_id),
      root_transform_(transform_graph_.CreateTransform()) {
  FX_DCHECK(session_id_);
  FX_DCHECK(flatland_presenter_);
  FX_DCHECK(link_system_);
  FX_DCHECK(uber_struct_queue_);

  zx_status_t status = peer_closed_waiter_.Begin(
      dispatcher(),
      [this](async_dispatcher_t* dispatcher, async::WaitOnce* wait, zx_status_t status,
             const zx_packet_signal_t* signal) { destroy_display_function_(); });
  FX_DCHECK(status == ZX_OK);

  FLATLAND_VERBOSE_LOG << "FlatlandDisplay new with ID: " << session_id_;
}

void FlatlandDisplay::SetContent(ViewportCreationToken token,
                                 fidl::InterfaceRequest<ChildViewWatcher> child_view_watcher) {
  // Attempting to link with an invalid token will never succeed, so its better to fail early and
  // immediately close the link connection.
  if (!token.value.is_valid()) {
    FX_LOGS(ERROR) << "CreateViewport failed, ViewportCreationToken was invalid";
    ReportError();
    return;
  }

  // TODO(fxbug.dev/76640): In order to replace content from a previous call to SetContent(), need
  // to detach from root_transform_, and otherwise clean up.  Flatland::ReleaseViewport() seems like
  // a good place to start.
  FX_CHECK(link_to_child_.parent_transform_handle == flatland::TransformHandle())
      << "Replacing FlatlandDisplay content is not yet supported.";

  auto child_transform = transform_graph_.CreateTransform();

  const auto kDevicePixelRatio = display_->device_pixel_ratio();
  ViewportProperties properties;
  {
    // To calculate the logical size, we need to divide the physical pixel size by the DPR.
    fuchsia::math::SizeU size;
    size.width = display_->width_in_px() / kDevicePixelRatio.x;
    size.height = display_->height_in_px() / kDevicePixelRatio.y;
    properties.set_logical_size(size);
    properties.set_inset({0, 0, 0, 0});
  }

  // We can initialize the Link importer immediately, since no state changes actually occur before
  // the feed-forward portion of this method. We also forward the initial ViewportProperties through
  // the LinkSystem immediately, so the child can receive them as soon as possible.
  // NOTE: clients won't receive CONNECTED_TO_DISPLAY until LinkSystem::UpdateLinks() is called,
  // typically during rendering.
  link_to_child_ = link_system_->CreateLinkToChild(
      dispatcher_holder_, std::move(token), fidl::Clone(properties), std::move(child_view_watcher),
      child_transform,
      [ref = weak_from_this(),
       dispatcher_holder = dispatcher_holder_](const std::string& error_log) {
        FX_CHECK(dispatcher_holder->dispatcher() == async_get_default_dispatcher())
            << "Link protocol error reported on the wrong dispatcher.";

        // TODO(fxbug.dev/77035): FlatlandDisplay currently has no way to notify clients of errors.
        FX_LOGS(ERROR) << "FlatlandDisplay illegal client usage: " << error_log;
      });
  FX_CHECK(child_transform == link_to_child_.parent_transform_handle);

  // This is the feed-forward portion of the method, i.e. the part which enqueues an updated
  // UberStruct.
  bool child_added;
  child_added = transform_graph_.AddChild(link_to_child_.parent_transform_handle,
                                          link_to_child_.internal_link_handle);
  FX_DCHECK(child_added);
  child_added = transform_graph_.AddChild(root_transform_, link_to_child_.parent_transform_handle);
  FX_DCHECK(child_added);

  // TODO(fxbug.dev/76640): given this fixed topology, we probably don't need to use
  // ComputeAndCleanup(), we can just stamp something out based on a fixed template.
  // TODO(fxbug.dev/40818): Decide on a proper limit on compute time for topological sorting.
  auto data =
      transform_graph_.ComputeAndCleanup(root_transform_, std::numeric_limits<uint64_t>::max());
  FX_DCHECK(data.iterations != std::numeric_limits<uint64_t>::max());
  FX_DCHECK(data.sorted_transforms[0].handle == root_transform_);

  auto uber_struct = std::make_unique<UberStruct>();
  uber_struct->debug_name = "FlatlandDisplay";
  uber_struct->local_topology = std::move(data.sorted_transforms);
  uber_struct->link_properties[link_to_child_.parent_transform_handle] = std::move(properties);
  uber_struct->local_clip_regions[link_to_child_.parent_transform_handle] = {
      .x = 0,
      .y = 0,
      .width = static_cast<int32_t>(display_->width_in_px()),
      .height = static_cast<int32_t>(display_->height_in_px()),
  };

  // By scaling the local matrix of the uberstruct here by the device pixel ratio, we ensure that
  // internally, the sizes of content on flatland instances that are hooked up to this display are
  // scaled up by the appropriate amount.
  uber_struct->local_matrices[link_to_child_.parent_transform_handle] =
      glm::scale(glm::mat3(), kDevicePixelRatio);

  auto present_id = scheduling::GetNextPresentId();
  uber_struct_queue_->Push(present_id, std::move(uber_struct));
  flatland_presenter_->ScheduleUpdateForSession(zx::time(0), {session_id_, present_id},
                                                /*squashable=*/true, /*release_fences=*/{});

  // TODO(fxbug.dev/76640): Flatland::Present() does:
  //    for (auto& operation : link_operations) { operation(); }
  // ... we should do something similar?  I believe that this will become necessary when we allow
  // SetContent() to be called more than once.
}

void FlatlandDisplay::SetDevicePixelRatio(fuchsia::math::VecF device_pixel_ratio) {
  if (device_pixel_ratio.x < 1.f || device_pixel_ratio.y < 1.f) {
    FX_LOGS(ERROR) << "SetDevicePixelRatio failed, device_pixel_ratio is invalid";
    ReportError();
    return;
  }

  display_->set_device_pixel_ratio({device_pixel_ratio.x, device_pixel_ratio.y});
}

}  // namespace flatland
