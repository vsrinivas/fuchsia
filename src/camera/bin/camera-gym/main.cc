// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/bridge.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <set>

#include "src/camera/bin/camera-gym/buffer_collage.h"
#include "src/camera/bin/camera-gym/lifecycle_impl.h"
#include "src/camera/bin/camera-gym/stream_cycler.h"

int main(int argc, char* argv[]) {
  syslog::SetLogSettings({.min_log_level = CAMERA_MIN_LOG_LEVEL}, {"camera-gym"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // Connect to required services for the collage.
  fuchsia::sysmem::AllocatorHandle allocator;
  zx_status_t status = context->svc()->Connect(allocator.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to request Allocator service.";
    return EXIT_FAILURE;
  }
  fuchsia::ui::scenic::ScenicHandle scenic;
  status = context->svc()->Connect(scenic.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to request Scenic service.";
    return EXIT_FAILURE;
  }
  fuchsia::ui::policy::PresenterHandle presenter;
  context->svc()->Connect(presenter.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to request Presenter service.";
    return EXIT_FAILURE;
  }

  // Create the collage.
  auto collage_result = camera::BufferCollage::Create(std::move(scenic), std::move(allocator),
                                                      std::move(presenter), [&] { loop.Quit(); });
  if (collage_result.is_error()) {
    FX_PLOGS(ERROR, collage_result.error()) << "Failed to create BufferCollage.";
    return EXIT_FAILURE;
  }
  auto collage = collage_result.take_value();

  // Connect to required services for the cycler.
  fuchsia::camera3::DeviceWatcherHandle watcher;
  status = context->svc()->Connect(watcher.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to request DeviceWatcher service.";
    return EXIT_FAILURE;
  }
  allocator = nullptr;
  status = context->svc()->Connect(allocator.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to request Allocator service.";
    return EXIT_FAILURE;
  }

  // Create the cycler and attach it to the collage.
  auto cycler_result = camera::StreamCycler::Create(std::move(watcher), std::move(allocator));
  if (cycler_result.is_error()) {
    FX_PLOGS(ERROR, cycler_result.error()) << "Failed to create StreamCycler.";
    return EXIT_FAILURE;
  }
  auto cycler = cycler_result.take_value();
  bool device_muted = false;
  std::set<uint32_t> collection_ids;

  camera::StreamCycler::AddCollectionHandler add_collection_handler =
      [&](fuchsia::sysmem::BufferCollectionTokenHandle token,
          fuchsia::sysmem::ImageFormat_2 image_format, std::string description) -> uint32_t {
    auto result = fit::run_single_threaded(
        collage->AddCollection(std::move(token), image_format, description));
    if (result.is_error()) {
      FX_LOGS(FATAL) << "Failed to add collection to collage.";
      return 0;
    }
    collection_ids.insert(result.value());
    return result.value();
  };

  camera::StreamCycler::RemoveCollectionHandler remove_collection_handler = [&](uint32_t id) {
    collection_ids.erase(id);
    collage->RemoveCollection(id);
  };

  camera::StreamCycler::ShowBufferHandler show_buffer_handler =
      [&](uint32_t collection_id, uint32_t buffer_index, zx::eventpair release_fence,
          std::optional<fuchsia::math::RectF> subregion) {
        collage->PostShowBuffer(collection_id, buffer_index, std::move(release_fence),
                                std::move(subregion));
        if (!device_muted) {
          // Only make the collection visible after we have shown an unmuted frame.
          collage->PostSetCollectionVisibility(collection_id, true);
        }
      };

  camera::StreamCycler::MuteStateHandler mute_handler = [&](bool muted) {
    collage->PostSetMuteIconVisibility(muted);
    if (muted) {
      // Immediately hide all collections on mute.
      for (auto id : collection_ids) {
        collage->PostSetCollectionVisibility(id, false);
      }
    }
    device_muted = muted;
  };

  cycler->SetHandlers(std::move(add_collection_handler), std::move(remove_collection_handler),
                      std::move(show_buffer_handler), std::move(mute_handler));

  // Publish the view service.
  context->outgoing()->AddPublicService(collage->GetHandler());

  // Publish a handler for the Lifecycle protocol that cleanly quits the component.
  LifecycleImpl lifecycle([&] { loop.Quit(); });
  context->outgoing()->AddPublicService(lifecycle.GetHandler());

  loop.Run();
  return EXIT_SUCCESS;
}
