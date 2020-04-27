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
#include <lib/syslog/cpp/logger.h>

#include "src/camera/bin/camera-gym/buffer_collage.h"
#include "src/camera/bin/camera-gym/lifecycle_impl.h"
#include "src/camera/bin/camera-gym/stream_cycler.h"

int main(int argc, char* argv[]) {
  syslog::SetTags({"camera-gym"});

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

  // Create the collage.
  auto collage_result = camera::BufferCollage::Create(std::move(scenic), std::move(allocator), [&] {
    FX_LOGS(INFO) << "BufferCollage stopped. Quitting loop.";
    loop.Quit();
  });
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
  cycler->SetHandlers(
      [&](fuchsia::sysmem::BufferCollectionTokenHandle token,
          fuchsia::sysmem::ImageFormat_2 image_format) -> uint32_t {
        auto result =
            fit::run_single_threaded(collage->AddCollection(std::move(token), image_format, ""));
        if (result.is_error()) {
          FX_LOGS(FATAL) << "Failed to add collection to collage.";
          return 0;
        }
        return result.value();
      },
      [&](uint32_t id) { collage->RemoveCollection(id); },
      [&](uint32_t collection_id, uint32_t buffer_index, zx::eventpair release_fence) {
        collage->PostShowBuffer(collection_id, buffer_index, std::move(release_fence),
                                std::nullopt);
      });

  // Connect to the device registry to listen for mute events.
  fuchsia::ui::policy::DeviceListenerRegistryPtr registry;
  registry.set_error_handler([](zx_status_t status) {
    FX_PLOGS(WARNING, status) << "DeviceListenerRegistry disconnected unexpectedly.";
  });
  status = context->svc()->Connect(registry.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to request DeviceListenerRegistry service.";
    return EXIT_FAILURE;
  }
  class Listener : public fuchsia::ui::policy::MediaButtonsListener {
   public:
    Listener(async_dispatcher_t* dispatcher, fit::function<void(bool)> on_mute_state_changed)
        : dispatcher_(dispatcher),
          binding_(this),
          on_mute_state_changed_(std::move(on_mute_state_changed)) {}
    fuchsia::ui::policy::MediaButtonsListenerHandle NewBinding() {
      return binding_.NewBinding(dispatcher_);
    }

   private:
    // |fuchsia::ui::policy::MediaButtonsListener|
    void OnMediaButtonsEvent(fuchsia::ui::input::MediaButtonsEvent event) override {
      if (event.has_mic_mute()) {
        bool muted = event.mic_mute();
        FX_LOGS(INFO) << "Mic and Camera are " << (muted ? "muted" : "unmuted") << ".";
        on_mute_state_changed_(muted);
      }
    }
    async_dispatcher_t* dispatcher_;
    fidl::Binding<fuchsia::ui::policy::MediaButtonsListener> binding_;
    fit::function<void(bool)> on_mute_state_changed_;
  } listener(loop.dispatcher(), [&](bool muted) { collage->PostSetVisibility(!muted); });
  registry->RegisterMediaButtonsListener(listener.NewBinding());

  // Publish the view service.
  context->outgoing()->AddPublicService(collage->GetHandler());

  // Publish a handler for the Lifecycle protocol that cleanly quits the component.
  LifecycleImpl lifecycle([&] { loop.Quit(); });
  context->outgoing()->AddPublicService(lifecycle.GetHandler());

  // Run the loop.
  status = loop.Run();
  if (status == ZX_ERR_CANCELED) {
    FX_LOGS(INFO) << "Main loop terminated normally.";
  } else {
    FX_LOGS(WARNING) << "Main loop terminated abnormally.";
    return EXIT_FAILURE;
  }
  return 0;
}
