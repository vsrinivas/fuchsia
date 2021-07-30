// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/bin/app.h"

#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "lib/async/default.h"
#include "rapidjson/document.h"
#include "src/lib/cobalt/cpp/cobalt_logger.h"
#include "src/lib/files/file.h"
#include "src/ui/lib/escher/vk/pipeline_builder.h"
#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"
#include "src/ui/scenic/lib/gfx/api/internal_snapshot_impl.h"
#include "src/ui/scenic/lib/gfx/gfx_system.h"
#include "src/ui/scenic/lib/scheduling/frame_metrics_registry.cb.h"
#include "src/ui/scenic/lib/scheduling/windowed_frame_predictor.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace {

// App installs the loader manifest FS at this path so it can use fsl::DeviceWatcher on
// it.
static const char* kDependencyPath = "/gpu-manifest-fs";

// Reads the config file and returns a struct with all found values.
scenic_impl::ConfigValues ReadConfig() {
  scenic_impl::ConfigValues values;

  std::string config_string;
  std::string flatland_buffer_collection_import_mode_str;
  if (files::ReadFileToString("/config/data/scenic_config", &config_string)) {
    FX_LOGS(INFO) << "Found config file at /config/data/scenic_config";

    rapidjson::Document document;
    document.Parse(config_string);

    int frame_scheduler_min_predicted_frame_duration_in_us = 0;
    if (document.HasMember("frame_scheduler_min_predicted_frame_duration_in_us")) {
      auto& val = document["frame_scheduler_min_predicted_frame_duration_in_us"];
      FX_CHECK(val.IsInt()) << "min_preducted_frame_duration must be an integer";
      frame_scheduler_min_predicted_frame_duration_in_us = val.GetInt();
      FX_CHECK(frame_scheduler_min_predicted_frame_duration_in_us >= 0);

      values.min_predicted_frame_duration =
          zx::usec(frame_scheduler_min_predicted_frame_duration_in_us);
    }

    if (document.HasMember("enable_allocator_for_flatland")) {
      auto& val = document["enable_allocator_for_flatland"];
      FX_CHECK(val.IsBool()) << "enable_allocator_for_flatland must be a boolean";
      values.enable_allocator_for_flatland = val.GetBool();
    }

    if (document.HasMember("pointer_auto_focus")) {
      auto& val = document["pointer_auto_focus"];
      FX_CHECK(val.IsBool()) << "pointer_auto_focus must be a boolean";
      values.pointer_auto_focus_on = val.GetBool();
    }

    if (document.HasMember("flatland_buffer_collection_import_mode")) {
      auto& val = document["flatland_buffer_collection_import_mode"];
      FX_CHECK(val.IsString()) << "flatland_buffer_collection_import_mode must be a string";
      flatland_buffer_collection_import_mode_str = val.GetString();
      values.flatland_buffer_collection_import_mode =
          flatland::StringToBufferCollectionImportMode(flatland_buffer_collection_import_mode_str);
    }
  } else {
    FX_LOGS(INFO) << "No config file found at /config/data/scenic_config; using default values";
  }

  FX_LOGS(INFO) << "Scenic min_predicted_frame_duration(us): "
                << values.min_predicted_frame_duration.to_usecs();
  FX_LOGS(INFO) << "enable_allocator_for_flatland: " << values.enable_allocator_for_flatland;
  FX_LOGS(INFO) << "Scenic pointer auto focus: " << values.pointer_auto_focus_on;
  FX_LOGS(INFO) << "flatland_buffer_collection_import_mode: "
                << (flatland_buffer_collection_import_mode_str.empty()
                        ? "attempt_display_constraints"
                        : flatland_buffer_collection_import_mode_str);

  return values;
}

// Specifies how often DoPeriodicLogging() is called.
constexpr zx::duration kPeriodicLogInterval{60'000'000'000};

}  // namespace

namespace scenic_impl {

DisplayInfoDelegate::DisplayInfoDelegate(std::shared_ptr<display::Display> display_)
    : display_(display_) {
  FX_CHECK(display_);
}

void DisplayInfoDelegate::GetDisplayInfo(
    fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) {
  auto info = ::fuchsia::ui::gfx::DisplayInfo();
  info.width_in_px = display_->width_in_px();
  info.height_in_px = display_->height_in_px();

  callback(std::move(info));
}

void DisplayInfoDelegate::GetDisplayOwnershipEvent(
    fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) {
  // These constants are defined as raw hex in the FIDL file, so we confirm here that they are the
  // same values as the expected constants in the ZX headers.
  static_assert(fuchsia::ui::scenic::displayNotOwnedSignal == ZX_USER_SIGNAL_0, "Bad constant");
  static_assert(fuchsia::ui::scenic::displayOwnedSignal == ZX_USER_SIGNAL_1, "Bad constant");

  zx::event dup;
  if (display_->ownership_event().duplicate(ZX_RIGHTS_BASIC, &dup) != ZX_OK) {
    FX_LOGS(ERROR) << "Display ownership event duplication error.";
    callback(zx::event());
  } else {
    callback(std::move(dup));
  }
}

App::App(std::unique_ptr<sys::ComponentContext> app_context, inspect::Node inspect_node,
         fpromise::promise<ui_display::DisplayControllerHandles> dc_handles_promise,
         fit::closure quit_callback)
    : executor_(async_get_default_dispatcher()),
      app_context_(std::move(app_context)),
      config_values_(ReadConfig()),
      // TODO(fxbug.dev/40997): subsystems requiring graceful shutdown *on a loop* should register
      // themselves. It is preferable to cleanly shutdown using destructors only, if possible.
      shutdown_manager_(
          ShutdownManager::New(async_get_default_dispatcher(), std::move(quit_callback))),
      scenic_(std::make_shared<Scenic>(app_context_.get(), std::move(inspect_node),
                                       [weak = std::weak_ptr<ShutdownManager>(shutdown_manager_)] {
                                         if (auto strong = weak.lock()) {
                                           strong->Shutdown(
                                               LifecycleControllerImpl::kShutdownTimeout);
                                         }
                                       })),
      uber_struct_system_(std::make_shared<flatland::UberStructSystem>()),
      link_system_(
          std::make_shared<flatland::LinkSystem>(uber_struct_system_->GetNextInstanceId())),
      flatland_presenter_(
          std::make_shared<flatland::DefaultFlatlandPresenter>(async_get_default_dispatcher())),
      annotation_registry_(app_context_.get()),
      lifecycle_controller_impl_(app_context_.get(),
                                 std::weak_ptr<ShutdownManager>(shutdown_manager_)) {
  FX_DCHECK(!device_watcher_);

  fpromise::bridge<escher::EscherUniquePtr> escher_bridge;
  fpromise::bridge<std::shared_ptr<display::Display>> display_bridge;

  auto vulkan_loader = app_context_->svc()->Connect<fuchsia::vulkan::loader::Loader>();
  fidl::InterfaceHandle<fuchsia::io::Directory> dir;
  vulkan_loader->ConnectToManifestFs(fuchsia::vulkan::loader::ConnectToManifestOptions{},
                                     dir.NewRequest().TakeChannel());

  fdio_ns_t* ns;
  zx_status_t status = fdio_ns_get_installed(&ns);
  FX_DCHECK(status == ZX_OK);
  status = fdio_ns_bind(ns, kDependencyPath, dir.TakeChannel().release());
  FX_DCHECK(status == ZX_OK);

  view_ref_installed_impl_.Publish(app_context_.get());

  // Wait for a Vulkan ICD to become advertised before trying to launch escher.
  device_watcher_ = fsl::DeviceWatcher::Create(
      kDependencyPath,
      [this, vulkan_loader = std::move(vulkan_loader),
       completer = std::move(escher_bridge.completer)](int dir_fd, std::string filename) mutable {
        auto escher = gfx::GfxSystem::CreateEscher(app_context_.get());
        if (!escher) {
          FX_LOGS(WARNING) << "Escher creation failed.";
          // This should almost never happen, but might if the device was removed quickly after it
          // was added or if the Vulkan driver doesn't actually work on this hardware. Retry when a
          // new device is added.
          return;
        }
        completer.complete_ok(std::move(escher));
        device_watcher_.reset();
      });

  FX_DCHECK(device_watcher_);

  // Instantiate DisplayManager and schedule a task to inject the display controller into it, once
  // it becomes available.
  display_manager_ = std::make_unique<display::DisplayManager>(
      [this, completer = std::move(display_bridge.completer)]() mutable {
        completer.complete_ok(display_manager_->default_display_shared());
      });
  executor_.schedule_task(dc_handles_promise.then(
      [this](fpromise::result<ui_display::DisplayControllerHandles>& handles) {
        display_manager_->BindDefaultDisplayController(std::move(handles.value().controller),
                                                       std::move(handles.value().dc_device));
      }));

  // Schedule a task to finish initialization once all promises have been completed.
  // This closure is placed on |executor_|, which is owned by App, so it is safe to use |this|.
  auto p =
      fpromise::join_promises(escher_bridge.consumer.promise(), display_bridge.consumer.promise())
          .and_then(
              [this](std::tuple<fpromise::result<escher::EscherUniquePtr>,
                                fpromise::result<std::shared_ptr<display::Display>>>& results) {
                InitializeServices(std::move(std::get<0>(results).value()),
                                   std::move(std::get<1>(results).value()));
                // Should be run after all outgoing services are published.
                app_context_->outgoing()->ServeFromStartupInfo();
              });

  executor_.schedule_task(std::move(p));

#ifdef NDEBUG
  // TODO(fxbug.dev/48596): Scenic sometimes gets stuck for consecutive 60 seconds.
  // Here we set up a Watchdog polling Scenic status every 15 seconds.
  constexpr uint32_t kWatchdogWarningIntervalMs = 15000u;
  // On some devices, the time to start up Scenic may exceed 15 seconds.
  // In that case we should only send a warning, and we should only crash
  // Scenic if the main thread is blocked for longer time.
  constexpr uint32_t kWatchdogTimeoutMs = 45000u;

#else  // !defined(NDEBUG)
  // We set a higher warning interval and timeout length for debug builds,
  // since these builds could be slower than the default release ones.
  constexpr uint32_t kWatchdogWarningIntervalMs = 30000u;
  constexpr uint32_t kWatchdogTimeoutMs = 90000u;

#endif  // NDEBUG

  watchdog_ = std::make_unique<async_watchdog::Watchdog>(
      "Scenic main thread", kWatchdogWarningIntervalMs, kWatchdogTimeoutMs,
      async_get_default_dispatcher());
}

void App::InitializeServices(escher::EscherUniquePtr escher,
                             std::shared_ptr<display::Display> display) {
  TRACE_DURATION("gfx", "App::InitializeServices");

  if (!display) {
    FX_LOGS(ERROR) << "No default display, Graphics system exiting";
    shutdown_manager_->Shutdown(LifecycleControllerImpl::kShutdownTimeout);
    return;
  }

  if (!escher || !escher->device()) {
    FX_LOGS(ERROR) << "No Vulkan on device, Graphics system exiting.";
    shutdown_manager_->Shutdown(LifecycleControllerImpl::kShutdownTimeout);
    return;
  }

  escher_ = std::move(escher);

  cobalt_logger_ = cobalt::NewCobaltLoggerFromProjectId(
      async_get_default_dispatcher(), app_context_->svc(), cobalt_registry::kProjectId);
  if (!cobalt_logger_) {
    FX_LOGS(ERROR) << "CobaltLogger creation failed!";
  }

  CreateFrameScheduler(display->vsync_timing());
  InitializeGraphics(display);
  InitializeInput();
  InitializeHeartbeat();

  periodic_logging_task_.PostDelayed(async_get_default_dispatcher(), kPeriodicLogInterval);
}

App::~App() {
  fdio_ns_t* ns;
  zx_status_t status = fdio_ns_get_installed(&ns);
  FX_DCHECK(status == ZX_OK);
  status = fdio_ns_unbind(ns, kDependencyPath);
  FX_DCHECK(status == ZX_OK);
}

void App::CreateFrameScheduler(std::shared_ptr<const scheduling::VsyncTiming> vsync_timing) {
  TRACE_DURATION("gfx", "App::CreateFrameScheduler");
  frame_scheduler_ = std::make_shared<scheduling::DefaultFrameScheduler>(
      std::move(vsync_timing),
      std::make_unique<scheduling::WindowedFramePredictor>(
          config_values_.min_predicted_frame_duration,
          scheduling::DefaultFrameScheduler::kInitialRenderDuration,
          scheduling::DefaultFrameScheduler::kInitialUpdateDuration),
      scenic_->inspect_node()->CreateChild("FrameScheduler"), cobalt_logger_);
}

void App::InitializeGraphics(std::shared_ptr<display::Display> display) {
  TRACE_DURATION("gfx", "App::InitializeGraphics");
  // Replace Escher's default pipeline builder with one which will log to Cobalt upon each
  // unexpected lazy pipeline creation.  This allows us to detect when this slips through our
  // testing and occurs in the wild.  In order to detect problems ASAP during development, debug
  // builds CHECK instead of logging to Cobalt.
  {
    auto pipeline_builder = std::make_unique<escher::PipelineBuilder>(escher_->vk_device());
    pipeline_builder->set_log_pipeline_creation_callback(
        [cobalt_logger = cobalt_logger_](const vk::GraphicsPipelineCreateInfo* graphics_info,
                                         const vk::ComputePipelineCreateInfo* compute_info) {
          // TODO(fxbug.dev/49972): pre-warm compute pipelines in addition to graphics pipelines.
          if (compute_info) {
            FX_LOGS(WARNING) << "Unexpected lazy creation of Vulkan compute pipeline.";
            return;
          }

#if !defined(NDEBUG)
          FX_CHECK(false)  // debug builds should crash for early detection
#else
          FX_LOGS(WARNING)  // release builds should log to Cobalt, see below.
#endif
              << "Unexpected lazy creation of Vulkan pipeline.";

          cobalt_logger->LogEvent(
              cobalt_registry::kScenicRareEventMetricId,
              cobalt_registry::ScenicRareEventMetricDimensionEvent_LazyPipelineCreation);
        });
    escher_->set_pipeline_builder(std::move(pipeline_builder));
  }

  auto gfx_buffer_collection_importer =
      std::make_shared<gfx::GfxBufferCollectionImporter>(escher_->GetWeakPtr());
  {
    TRACE_DURATION("gfx", "App::InitializeServices[engine]");
    engine_ =
        std::make_shared<gfx::Engine>(escher_->GetWeakPtr(), gfx_buffer_collection_importer,
                                      scenic_->inspect_node()->CreateChild("Engine"),
                                      /*request_focus*/
                                      [this](zx_koid_t requestor, zx_koid_t request) {
                                        FX_DCHECK(focus_manager_);
                                        return focus_manager_->RequestFocus(requestor, request) ==
                                               focus::FocusChangeStatus::kAccept;
                                      });
  }

  scenic_->SetFrameScheduler(frame_scheduler_);
  annotation_registry_.InitializeWithGfxAnnotationManager(engine_->annotation_manager());

  image_pipe_updater_ = std::make_shared<gfx::ImagePipeUpdater>(frame_scheduler_);
  auto gfx = scenic_->RegisterSystem<gfx::GfxSystem>(engine_.get(), &sysmem_,
                                                     display_manager_.get(), image_pipe_updater_);
  FX_DCHECK(gfx);

  scenic_->SetScreenshotDelegate(gfx.get());
  display_info_delegate_ = std::make_unique<DisplayInfoDelegate>(display);
  scenic_->SetDisplayInfoDelegate(display_info_delegate_.get());

  flatland_presenter_->SetFrameScheduler(frame_scheduler_);

  // Create the snapshotter and pass it to scenic.
  auto snapshotter =
      std::make_unique<gfx::InternalSnapshotImpl>(engine_->scene_graph(), escher_->GetWeakPtr());
  scenic_->InitializeSnapshotService(std::move(snapshotter));
  scenic_->SetViewFocuserRegistry(engine_->scene_graph());

  // Flatland compositor must be made first; it is needed by the manager and the engine.
  {
    TRACE_DURATION("gfx", "App::InitializeServices[flatland_display_compositor]");

    auto flatland_renderer = std::make_shared<flatland::VkRenderer>(escher_->GetWeakPtr());

    flatland_compositor_ = std::make_shared<flatland::DisplayCompositor>(
        async_get_default_dispatcher(), display_manager_->default_display_controller(),
        flatland_renderer, utils::CreateSysmemAllocatorSyncPtr("flatland::DisplayCompositor"),
        config_values_.flatland_buffer_collection_import_mode);
  }

  // Flatland manager depends on compositor, and is required by engine.
  {
    TRACE_DURATION("gfx", "App::InitializeServices[flatland_manager]");

    std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> importers{
        flatland_compositor_};

    flatland_manager_ = std::make_shared<flatland::FlatlandManager>(
        async_get_default_dispatcher(), flatland_presenter_, uber_struct_system_, link_system_,
        display, std::move(importers));

    // TODO(fxbug.dev/67206): these should be moved into FlatlandManager.
    {
      fit::function<void(fidl::InterfaceRequest<fuchsia::ui::composition::Flatland>)> handler =
          fit::bind_member(flatland_manager_.get(), &flatland::FlatlandManager::CreateFlatland);
      zx_status_t status = app_context_->outgoing()->AddPublicService(std::move(handler));
      FX_DCHECK(status == ZX_OK);
    }
    {
      fit::function<void(fidl::InterfaceRequest<fuchsia::ui::composition::FlatlandDisplay>)>
          handler = fit::bind_member(flatland_manager_.get(),
                                     &flatland::FlatlandManager::CreateFlatlandDisplay);
      zx_status_t status = app_context_->outgoing()->AddPublicService(std::move(handler));
      FX_DCHECK(status == ZX_OK);
    }
  }

  // Allocator service needs Flatland DisplayCompositor to act as a BufferCollectionImporter.
  {
    std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> importers;
    std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> screenshot_importers;
    importers.push_back(gfx_buffer_collection_importer);
    if (config_values_.enable_allocator_for_flatland && flatland_compositor_)
      importers.push_back(flatland_compositor_);

    allocator_ = std::make_shared<allocation::Allocator>(
        app_context_.get(), importers, screenshot_importers,
        utils::CreateSysmemAllocatorSyncPtr("ScenicAllocator"));
  }

  // Flatland engine requires FlatlandManager and DisplayCompositor to be constructed first.
  {
    TRACE_DURATION("gfx", "App::InitializeServices[flatland_engine]");

    flatland_engine_ = std::make_shared<flatland::Engine>(flatland_compositor_, flatland_presenter_,
                                                          uber_struct_system_, link_system_);

    frame_renderer_ = std::make_shared<TemporaryFrameRendererDelegator>(flatland_manager_,
                                                                        flatland_engine_, engine_);
  }
}

void App::InitializeInput() {
  TRACE_DURATION("gfx", "App::InitializeInput");
  input_ = scenic_->RegisterSystem<input::InputSystem>(
      engine_->scene_graph(),
      /*request_focus*/ [this,
                         use_auto_focus = config_values_.pointer_auto_focus_on](zx_koid_t koid) {
        if (!use_auto_focus)
          return;

        const auto& focus_chain = focus_manager_->focus_chain();
        if (!focus_chain.empty()) {
          const zx_koid_t requestor = focus_chain[0];
          const zx_koid_t request = koid != ZX_KOID_INVALID ? koid : requestor;
          focus_manager_->RequestFocus(requestor, request);
        }
      });
  FX_DCHECK(input_);
  scenic_->SetRegisterTouchSource(
      [this](fidl::InterfaceRequest<fuchsia::ui::pointer::TouchSource> touch_source,
             zx_koid_t vrf) { input_->RegisterTouchSource(std::move(touch_source), vrf); });
  scenic_->SetRegisterMouseSource(
      [this](fidl::InterfaceRequest<fuchsia::ui::pointer::MouseSource> mouse_source,
             zx_koid_t vrf) { input_->RegisterMouseSource(std::move(mouse_source), vrf); });

  focus_manager_ = std::make_unique<focus::FocusManager>(
      scenic_->inspect_node()->CreateChild("FocusManager"),
      /*legacy_focus_listener*/ [this](zx_koid_t old_focus, zx_koid_t new_focus) {
        engine_->scene_graph()->OnNewFocusedView(old_focus, new_focus);
      });
  scenic_->SetViewRefFocusedRegisterFunction(
      [this](zx_koid_t koid, fidl::InterfaceRequest<fuchsia::ui::views::ViewRefFocused> vrf) {
        focus_manager_->RegisterViewRefFocused(koid, std::move(vrf));
      });
  focus_manager_->Publish(*app_context_);
}

void App::InitializeHeartbeat() {
  TRACE_DURATION("gfx", "App::InitializeHeartbeat");
  {  // Initialize ViewTreeSnapshotter
    std::vector<view_tree::SubtreeSnapshotGenerator> subtrees;
    subtrees.emplace_back([this] { return engine_->scene_graph()->view_tree().Snapshot(); });

    std::vector<view_tree::ViewTreeSnapshotter::Subscriber> subscribers;
    subscribers.push_back(
        {.on_new_view_tree =
             [this](auto snapshot) { input_->OnNewViewTreeSnapshot(std::move(snapshot)); },
         .dispatcher = async_get_default_dispatcher()});

    subscribers.push_back(
        {.on_new_view_tree =
             [this](auto snapshot) { focus_manager_->OnNewViewTreeSnapshot(std::move(snapshot)); },
         .dispatcher = async_get_default_dispatcher()});

    subscribers.push_back({.on_new_view_tree =
                               [this](auto snapshot) {
                                 view_ref_installed_impl_.OnNewViewTreeSnapshot(
                                     std::move(snapshot));
                               },
                           .dispatcher = async_get_default_dispatcher()});
    view_tree_snapshotter_ = std::make_shared<view_tree::ViewTreeSnapshotter>(
        std::move(subtrees), std::move(subscribers));
  }

  // |session_updaters| will be updated in submission order.
  frame_scheduler_->Initialize(
      /*frame_renderer*/ frame_renderer_,
      /*session_updaters*/ {scenic_, image_pipe_updater_, flatland_manager_, flatland_presenter_,
                            view_tree_snapshotter_});
}

void App::DoPeriodicLogging(async_dispatcher_t* dispatcher, async::TaskBase*, zx_status_t) {
  frame_scheduler_->LogPeriodicDebugInfo();

  periodic_logging_task_.PostDelayed(dispatcher, kPeriodicLogInterval);
}

}  // namespace scenic_impl
