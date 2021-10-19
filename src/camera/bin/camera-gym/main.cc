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
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include <set>

#include "src/camera/bin/camera-gym/buffer_collage.h"
#include "src/camera/bin/camera-gym/controller_receiver.h"
#include "src/camera/bin/camera-gym/lifecycle_impl.h"
#include "src/camera/bin/camera-gym/stream_cycler.h"
#include "src/lib/fxl/command_line.h"

using Command = fuchsia::camera::gym::Command;

int main(int argc, char* argv[]) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  // Default must match existing behavior as started from UI.
  bool manual_mode = command_line.HasOption("manual");

  syslog::SetLogSettings({.min_log_level = CAMERA_MIN_LOG_LEVEL}, {"camera-gym"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
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
  auto collage_result = camera::BufferCollage::Create(
      std::move(scenic), std::move(allocator), std::move(presenter), [&loop] { loop.Quit(); });
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

  // Create the async::Loop to be used privately by StreamCycler.
  async::Loop cycler_loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  // Create the cycler and attach it to the collage.
  auto cycler_result = camera::StreamCycler::Create(std::move(watcher), std::move(allocator),
                                                    cycler_loop.dispatcher(), manual_mode);
  if (cycler_result.is_error()) {
    FX_PLOGS(ERROR, cycler_result.error()) << "Failed to create StreamCycler.";
    return EXIT_FAILURE;
  }
  auto cycler = cycler_result.take_value();

  status = cycler_loop.StartThread("StreamCycler Thread");
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return EXIT_FAILURE;
  }

  bool device_muted = false;
  std::set<uint32_t> collection_ids;

  camera::StreamCycler::AddCollectionHandler add_collection_handler =
      [&collage, &collection_ids](fuchsia::sysmem::BufferCollectionTokenHandle token,
                                  fuchsia::sysmem::ImageFormat_2 image_format,
                                  std::string description) -> uint32_t {
    auto result = fpromise::run_single_threaded(
        collage->AddCollection(std::move(token), image_format, description));
    if (result.is_error()) {
      FX_LOGS(FATAL) << "Failed to add collection to collage.";
      return 0;
    }
    collection_ids.insert(result.value());
    return result.value();
  };

  camera::StreamCycler::RemoveCollectionHandler remove_collection_handler =
      [&collage, &collection_ids](uint32_t id) {
        collection_ids.erase(id);
        collage->RemoveCollection(id);
      };

  camera::StreamCycler::ShowBufferHandler show_buffer_handler =
      [&collage, &device_muted](uint32_t collection_id, uint32_t buffer_index,
                                zx::eventpair release_fence,
                                std::optional<fuchsia::math::RectF> subregion) {
        collage->PostShowBuffer(collection_id, buffer_index, std::move(release_fence),
                                std::move(subregion));
        if (!device_muted) {
          // Only make the collection visible after we have shown an unmuted frame.
          collage->PostSetCollectionVisibility(collection_id, true);
        }
      };

  camera::StreamCycler::MuteStateHandler mute_handler = [&collage, &collection_ids,
                                                         &device_muted](bool muted) {
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

  std::unique_ptr<camera::ControllerReceiver> controller_receiver;
  if (manual_mode) {
    controller_receiver = std::make_unique<camera::ControllerReceiver>();

    FX_LOGS(INFO) << "Running in manual mode.";

    // Bridge ControllerReceiver to StreamCycler.
    camera::ControllerReceiver::CommandHandler command_handler =
        [&cycler, &collage](fuchsia::camera::gym::Command command,
                            camera::ControllerReceiver::SendCommandCallback callback) {
          if (command.Which() == Command::Tag::kSetDescription) {
            collage->ExecuteCommand(std::move(command), std::move(callback));
          } else {
            cycler->ExecuteCommand(std::move(command), std::move(callback));
          }
        };
    controller_receiver->SetHandlers(std::move(command_handler));

    context->outgoing()->AddPublicService(controller_receiver->GetHandler());
  } else {
    FX_LOGS(INFO) << "Running in automatic mode.";
  }

  // Publish the view service.
  context->outgoing()->AddPublicService(collage->GetHandler());

  // Publish a handler for the Lifecycle protocol that cleanly quits the component.
  LifecycleImpl lifecycle([&loop] { loop.Quit(); });
  context->outgoing()->AddPublicService(lifecycle.GetHandler());

  loop.Run();
  cycler_loop.Quit();
  cycler_loop.JoinThreads();
  return EXIT_SUCCESS;
}
