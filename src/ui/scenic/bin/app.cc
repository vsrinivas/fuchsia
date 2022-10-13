// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/bin/app.h"

#include <fuchsia/stash/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>

#include <optional>

#include "rapidjson/document.h"
#include "src/lib/files/file.h"
#include "src/ui/lib/escher/vk/pipeline_builder.h"
#include "src/ui/scenic/lib/display/display_power_manager.h"
#include "src/ui/scenic/lib/flatland/engine/color_converter.h"
#include "src/ui/scenic/lib/flatland/engine/engine_types.h"
#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"
#include "src/ui/scenic/lib/gfx/api/internal_snapshot_impl.h"
#include "src/ui/scenic/lib/gfx/engine/color_converter.h"
#include "src/ui/scenic/lib/gfx/gfx_system.h"
#include "src/ui/scenic/lib/gfx/screenshotter.h"
#include "src/ui/scenic/lib/scheduling/frame_metrics_registry.cb.h"
#include "src/ui/scenic/lib/scheduling/windowed_frame_predictor.h"
#include "src/ui/scenic/lib/screen_capture/screen_capture.h"
#include "src/ui/scenic/lib/screen_capture/screen_capture_buffer_collection_importer.h"
#include "src/ui/scenic/lib/screenshot/screenshot_manager.h"
#include "src/ui/scenic/lib/utils/helpers.h"
#include "src/ui/scenic/lib/utils/metrics_impl.h"
#include "src/ui/scenic/lib/view_tree/snapshot_dump.h"

namespace {

// App installs the loader manifest FS at this path so it can use
// fsl::DeviceWatcher on it.
static const char* kDependencyPath = "/gpu-manifest-fs";

// Populates a ConfigValues struct by reading a config file and retrieving
// overrides from the stash.
scenic_impl::ConfigValues GetConfig(sys::ComponentContext* app_context) {
  scenic_impl::ConfigValues values;

  using GetValueCallback = std::function<void(const std::string&, fuchsia::stash::Value&)>;
  std::unordered_map<std::string, GetValueCallback> config{
      {
          "frame_scheduler_min_predicted_frame_duration_in_us",
          [&values](auto& key, auto& value) {
            FX_CHECK(value.is_intval()) << key << " must be an integer";
            FX_CHECK(value.intval() >= 0) << key << " must be greater than 0";
            values.min_predicted_frame_duration = zx::usec(value.intval());
          },
      },
      {
          "i_can_haz_flatland",
          [&values](auto& key, auto& value) {
            FX_CHECK(value.is_boolval()) << key << " must be a boolean";
            values.i_can_haz_flatland = value.boolval();
          },
      },
      {
          "enable_allocator_for_flatland",
          [&values](auto& key, auto& value) {
            FX_CHECK(value.is_boolval()) << key << " must be a boolean";
            values.enable_allocator_for_flatland = value.boolval();
          },
      },
      {
          "pointer_auto_focus",
          [&values](auto& key, auto& value) {
            FX_CHECK(value.is_boolval()) << key << " must be a boolean";
            values.pointer_auto_focus_on = value.boolval();
          },
      },
      {
          "flatland_buffer_collection_import_mode",
          [&values](auto& key, auto& value) {
            FX_CHECK(value.is_stringval()) << key << " must be a string";
            values.flatland_buffer_collection_import_mode =
                flatland::StringToBufferCollectionImportMode(value.stringval());
          },
      },
      {
          "i_can_haz_display_id",
          [&values](auto& key, auto& value) {
            FX_CHECK(value.is_intval()) << key << " must be an integer";
            values.i_can_haz_display_id = value.intval();
          },
      },
      {
          "i_can_haz_display_mode",
          [&values](auto& key, auto& value) {
            FX_CHECK(value.is_intval()) << key << " must be an integer";
            values.i_can_haz_display_mode = value.intval();
          },
      },
  };

  async::Loop stash_loop(&kAsyncLoopConfigNeverAttachToThread);
  fuchsia::stash::StorePtr store;
  fuchsia::stash::StoreAccessorPtr accessor;
  zx_status_t status = app_context->svc()->Connect(store.NewRequest(stash_loop.dispatcher()));
  if (status == ZX_OK) {
    store->Identify("stash_ctl");
    store->CreateAccessor(true, accessor.NewRequest(stash_loop.dispatcher()));
  } else {
    FX_LOGS(INFO) << "Unable to access /svc/" << fuchsia::stash::Store::Name_
                  << "; using only config file";
  }

  // Request all stash values asynchronously. We do this before reading the
  // config file so we hide the cost of the asynchronous requests behind the
  // synchronous filesystem server request.
  for (auto& [key, callback] : config) {
    accessor->GetValue(key, [&key = key, &callback = callback](auto value) {
      if (value) {
        callback(key, *value);
      };
    });
  }

  std::string config_string;
  if (files::ReadFileToString("/config/data/scenic_config", &config_string)) {
    FX_LOGS(INFO) << "Found config file at /config/data/scenic_config";
    rapidjson::Document document;
    document.Parse(config_string);
    for (auto& [key, callback] : config) {
      if (document.HasMember(key)) {
        auto& json_value = document[key];

        fuchsia::stash::Value value;
        if (json_value.IsInt()) {
          value = fuchsia::stash::Value::WithIntval(json_value.GetInt());
        } else if (json_value.IsBool()) {
          value = fuchsia::stash::Value::WithBoolval(json_value.GetBool());
        } else if (json_value.IsString()) {
          value = fuchsia::stash::Value::WithStringval(json_value.GetString());
        } else {
          FX_CHECK(false) << "Unsupported type for '" << key << "'";
        }
        callback(key, value);
      }
    }
  } else {
    FX_LOGS(INFO) << "No config file found at /config/data/scenic_config; using default values";
  }

  // Wait for each stash value to be returned. These should have arrived while
  // reading the config file.
  //
  // Note: The order of these operations means that the stash will override any
  // values set by the config file.
  for (auto& _ : config) {
    // Only run the loop if the accessor is still bound.
    if (!accessor) {
      break;
    }
    stash_loop.Run(zx::time::infinite(), /*once*/ true);
  }

  // If we are disabling display composition, then disable display import constraints.
  if (flatland::DisplayCompositor::kDisableDisplayComposition) {
    values.flatland_buffer_collection_import_mode =
        flatland::BufferCollectionImportMode::RendererOnly;
  }

  FX_LOGS(INFO) << "Scenic min_predicted_frame_duration(us): "
                << values.min_predicted_frame_duration.to_usecs();
  FX_LOGS(INFO) << "i_can_haz_flatland: " << values.i_can_haz_flatland;
  FX_LOGS(INFO) << "enable_allocator_for_flatland: " << values.enable_allocator_for_flatland;
  FX_LOGS(INFO) << "Scenic pointer auto focus: " << values.pointer_auto_focus_on;
  FX_LOGS(INFO) << "flatland_buffer_collection_import_mode: "
                << StringFromBufferCollectionImportMode(
                       values.flatland_buffer_collection_import_mode);
  FX_LOGS(INFO) << "Scenic i_can_haz_display_id: " << values.i_can_haz_display_id.value_or(0);
  FX_LOGS(INFO) << "Scenic i_can_haz_display_mode: " << values.i_can_haz_display_mode.value_or(0);

  return values;
}

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

fuchsia::math::SizeU DisplayInfoDelegate::GetDisplayDimensions() {
  return {display_->width_in_px(), display_->height_in_px()};
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
      config_values_(GetConfig(app_context_.get())),
      // TODO(fxbug.dev/40997): subsystems requiring graceful shutdown *on a loop* should register
      // themselves. It is preferable to cleanly shutdown using destructors only, if possible.
      shutdown_manager_(
          ShutdownManager::New(async_get_default_dispatcher(), std::move(quit_callback))),
      metrics_logger_(
          async_get_default_dispatcher(),
          fidl::ClientEnd<fuchsia_io::Directory>(component::OpenServiceRoot()->TakeChannel())),
      inspect_node_(std::move(inspect_node)),
      frame_scheduler_(std::make_unique<scheduling::WindowedFramePredictor>(
                           config_values_.min_predicted_frame_duration,
                           scheduling::DefaultFrameScheduler::kInitialRenderDuration,
                           scheduling::DefaultFrameScheduler::kInitialUpdateDuration),
                       inspect_node_.CreateChild("FrameScheduler"), &metrics_logger_),
      scenic_(std::make_shared<Scenic>(
          app_context_.get(), inspect_node_, frame_scheduler_,
          [weak = std::weak_ptr<ShutdownManager>(shutdown_manager_)] {
            if (auto strong = weak.lock()) {
              strong->Shutdown(LifecycleControllerImpl::kShutdownTimeout);
            }
          },
          config_values_.i_can_haz_flatland)),
      uber_struct_system_(std::make_shared<flatland::UberStructSystem>()),
      link_system_(
          std::make_shared<flatland::LinkSystem>(uber_struct_system_->GetNextInstanceId())),
      flatland_presenter_(std::make_shared<flatland::FlatlandPresenterImpl>(
          async_get_default_dispatcher(), frame_scheduler_)),
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
      config_values_.i_can_haz_display_id, config_values_.i_can_haz_display_mode,
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

  InitializeGraphics(display);
  InitializeInput();
  InitializeHeartbeat(*display);
}

App::~App() {
  fdio_ns_t* ns;
  zx_status_t status = fdio_ns_get_installed(&ns);
  FX_DCHECK(status == ZX_OK);
  status = fdio_ns_unbind(ns, kDependencyPath);
  FX_DCHECK(status == ZX_OK);
}

void App::InitializeGraphics(std::shared_ptr<display::Display> display) {
  TRACE_DURATION("gfx", "App::InitializeGraphics");
  FX_LOGS(INFO) << "App::InitializeGraphics() " << display->width_in_px() << "x"
                << display->height_in_px() << "px  " << display->width_in_mm() << "x"
                << display->height_in_mm() << "mm";

  // Replace Escher's default pipeline builder with one which will log to Cobalt upon each
  // unexpected lazy pipeline creation.  This allows us to detect when this slips through our
  // testing and occurs in the wild.  In order to detect problems ASAP during development, debug
  // builds CHECK instead of logging to Cobalt.
  {
    auto pipeline_builder = std::make_unique<escher::PipelineBuilder>(escher_->vk_device());
    pipeline_builder->set_log_pipeline_creation_callback(
        [metrics_logger = &metrics_logger_](const vk::GraphicsPipelineCreateInfo* graphics_info,
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

          metrics_logger->LogRareEvent(
              cobalt_registry::ScenicRareEventMigratedMetricDimensionEvent::LazyPipelineCreation);
        });
    escher_->set_pipeline_builder(std::move(pipeline_builder));
  }

  auto gfx_buffer_collection_importer =
      std::make_shared<gfx::GfxBufferCollectionImporter>(escher_->GetWeakPtr());
  {
    TRACE_DURATION("gfx", "App::InitializeServices[engine]");
    engine_ = std::make_shared<gfx::Engine>(escher_->GetWeakPtr(), gfx_buffer_collection_importer,
                                            inspect_node_.CreateChild("Engine"));

    if (!config_values_.i_can_haz_flatland) {
      color_converter_ =
          std::make_unique<gfx::ColorConverter>(app_context_.get(), engine_->scene_graph());
    }
  }

  annotation_registry_.InitializeWithGfxAnnotationManager(engine_->annotation_manager());

  image_pipe_updater_ = std::make_shared<gfx::ImagePipeUpdater>(frame_scheduler_);
  auto gfx = scenic_->RegisterSystem<gfx::GfxSystem>(engine_.get(), &sysmem_,
                                                     display_manager_.get(), image_pipe_updater_);
  FX_DCHECK(gfx);

  scenic_->SetScreenshotDelegate(gfx.get());
  singleton_display_service_ = std::make_unique<display::SingletonDisplayService>(display);
  singleton_display_service_->AddPublicService(scenic_->app_context()->outgoing().get());
  display_info_delegate_ = std::make_unique<DisplayInfoDelegate>(display);
  scenic_->SetDisplayInfoDelegate(display_info_delegate_.get());

  // Create the snapshotter and pass it to scenic.
  auto snapshotter =
      std::make_unique<gfx::InternalSnapshotImpl>(engine_->scene_graph(), escher_->GetWeakPtr());
  scenic_->InitializeSnapshotService(std::move(snapshotter));
  scenic_->SetRegisterViewFocuser(
      [this](zx_koid_t view_ref_koid, fidl::InterfaceRequest<fuchsia::ui::views::Focuser> focuser) {
        focus_manager_->RegisterViewFocuser(view_ref_koid, std::move(focuser));
      });
  auto flatland_renderer = std::make_shared<flatland::VkRenderer>(escher_->GetWeakPtr());

  // Flatland compositor must be made first; it is needed by the manager and the engine.
  {
    TRACE_DURATION("gfx", "App::InitializeServices[flatland_display_compositor]");

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
        display, std::move(importers),
        /*register_view_focuser*/
        [this](fidl::InterfaceRequest<fuchsia::ui::views::Focuser> focuser,
               zx_koid_t view_ref_koid) {
          focus_manager_->RegisterViewFocuser(view_ref_koid, std::move(focuser));
        },
        /*register_view_ref_focused*/
        [this](fidl::InterfaceRequest<fuchsia::ui::views::ViewRefFocused> vrf,
               zx_koid_t view_ref_koid) {
          focus_manager_->RegisterViewRefFocused(view_ref_koid, std::move(vrf));
        },
        /*register_touch_source*/
        [this](fidl::InterfaceRequest<fuchsia::ui::pointer::TouchSource> touch_source,
               zx_koid_t view_ref_koid) {
          input_->RegisterTouchSource(std::move(touch_source), view_ref_koid);
        },
        /*register_mouse_source*/
        [this](fidl::InterfaceRequest<fuchsia::ui::pointer::MouseSource> mouse_source,
               zx_koid_t view_ref_koid) {
          input_->RegisterMouseSource(std::move(mouse_source), view_ref_koid);
        });

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

  // TODO(fxbug.dev/103678): Remove this once we establish prunable token based allocations in
  // ScreenCaptureBufferCollectionImporter.
  // For current devices, emulators are the only one which require copying into a CPU-accessible
  // buffer, because render targets cannot be CPU-accesible.
  const bool using_virtual_gpu = escher_->vk_physical_device().getProperties().deviceType ==
                                 vk::PhysicalDeviceType::eVirtualGpu;
  auto screen_capture_buffer_collection_importer =
      std::make_shared<screen_capture::ScreenCaptureBufferCollectionImporter>(
          utils::CreateSysmemAllocatorSyncPtr("ScreenCaptureBufferCollectionImporter"),
          flatland_renderer, /*enable_copy_fallback=*/using_virtual_gpu);

  // Allocator service needs Flatland DisplayCompositor to act as a BufferCollectionImporter.
  {
    std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> default_importers;
    std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> screen_capture_importers;
    default_importers.push_back(gfx_buffer_collection_importer);
    screen_capture_importers.push_back(screen_capture_buffer_collection_importer);

    if (config_values_.enable_allocator_for_flatland && flatland_compositor_)
      default_importers.push_back(flatland_compositor_);

    allocator_ = std::make_shared<allocation::Allocator>(
        app_context_.get(), default_importers, screen_capture_importers,
        utils::CreateSysmemAllocatorSyncPtr("ScenicAllocator"));
  }

  // Flatland engine requires FlatlandManager and DisplayCompositor to be constructed first.
  {
    TRACE_DURATION("gfx", "App::InitializeServices[flatland_engine]");

    flatland_engine_ = std::make_shared<flatland::Engine>(
        flatland_compositor_, flatland_presenter_, uber_struct_system_, link_system_,
        inspect_node_.CreateChild("FlatlandEngine"), [this] {
          FX_DCHECK(flatland_manager_);
          const auto display = flatland_manager_->GetPrimaryFlatlandDisplayForRendering();
          return display ? std::optional<flatland::TransformHandle>(display->root_transform())
                         : std::nullopt;
        });

    if (config_values_.i_can_haz_flatland) {
      color_converter_ =
          std::make_unique<flatland::ColorConverter>(app_context_.get(), flatland_compositor_);
    }

    frame_renderer_ = std::make_shared<TemporaryFrameRendererDelegator>(flatland_manager_,
                                                                        flatland_engine_, engine_);
  }

  // Make ScreenCaptureManager.
  {
    TRACE_DURATION("gfx", "App::InitializeServices[screen_capture_manager]");

    std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> screen_capture_importers;
    screen_capture_importers.push_back(screen_capture_buffer_collection_importer);

    // Capture flatland_manager since the primary display may not have been initialized yet.
    screen_capture_manager_ = std::make_unique<screen_capture::ScreenCaptureManager>(
        flatland_engine_, flatland_renderer, flatland_manager_,
        std::move(screen_capture_importers));

    fit::function<void(fidl::InterfaceRequest<fuchsia::ui::composition::ScreenCapture>)> handler =
        fit::bind_member(screen_capture_manager_.get(),
                         &screen_capture::ScreenCaptureManager::CreateClient);
    zx_status_t status = app_context_->outgoing()->AddPublicService(std::move(handler));
    FX_DCHECK(status == ZX_OK);
  }

  // Make ScreenCapture2Manager.
  {
    TRACE_DURATION("gfx", "App::InitializeServices[screen_capture2_manager]");

    // Capture flatland_manager since the primary display may not have been initialized yet.
    screen_capture2_manager_ = std::make_shared<screen_capture2::ScreenCapture2Manager>(
        flatland_renderer, screen_capture_buffer_collection_importer, [this]() {
          FX_DCHECK(flatland_manager_);
          FX_DCHECK(flatland_engine_);

          auto display = flatland_manager_->GetPrimaryFlatlandDisplayForRendering();
          FX_DCHECK(display);

          return flatland_engine_->GetRenderables(*display);
        });

    fit::function<void(fidl::InterfaceRequest<fuchsia::ui::composition::internal::ScreenCapture>)>
        handler = fit::bind_member(screen_capture2_manager_.get(),
                                   &screen_capture2::ScreenCapture2Manager::CreateClient);
    zx_status_t status = app_context_->outgoing()->AddPublicService(std::move(handler));
    FX_DCHECK(status == ZX_OK);
  }

  // Make ScreenshotManager for the client-friendly screenshot protocol.
  {
    TRACE_DURATION("gfx", "App::InitializeServices[screenshot_manager]");

    std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> screen_capture_importers;
    screen_capture_importers.push_back(screen_capture_buffer_collection_importer);

    // Capture flatland_manager since the primary display may not have been initialized yet.
    screenshot_manager_ = std::make_unique<screenshot::ScreenshotManager>(
        config_values_.i_can_haz_flatland, allocator_, flatland_renderer,
        [this]() {
          auto display = flatland_manager_->GetPrimaryFlatlandDisplayForRendering();
          return flatland_engine_->GetRenderables(*display);
        },
        [this](fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) {
          gfx::Screenshotter::TakeScreenshot(engine_.get(), std::move(callback));
        },
        std::move(screen_capture_importers), display_info_delegate_->GetDisplayDimensions());

    fit::function<void(fidl::InterfaceRequest<fuchsia::ui::composition::Screenshot>)> handler =
        fit::bind_member(screenshot_manager_.get(), &screenshot::ScreenshotManager::CreateBinding);
    zx_status_t status = app_context_->outgoing()->AddPublicService(std::move(handler));
    FX_DCHECK(status == ZX_OK);
  }

  {
    TRACE_DURATION("gfx", "App::InitializeServices[display_power]");
    display_power_manager_ = std::make_unique<display::DisplayPowerManager>(display_manager_.get());
    zx_status_t status =
        app_context_->outgoing()->AddPublicService(display_power_manager_->GetHandler());
    FX_DCHECK(status == ZX_OK);
  }

  geometry_provider_ = std::make_shared<view_tree::GeometryProvider>();

  observer_registry_ = std::make_unique<view_tree::Registry>(geometry_provider_);
  observer_registry_->Publish(app_context_.get());

  scoped_observer_registry_ = std::make_unique<view_tree::ScopedRegistry>(geometry_provider_);
  scoped_observer_registry_->Publish(app_context_.get());
}

void App::InitializeInput() {
  TRACE_DURATION("gfx", "App::InitializeInput");
  input_ = std::make_unique<input::InputSystem>(
      app_context_.get(), inspect_node_, engine_->scene_graph(),
      /*request_focus*/
      [this, use_auto_focus = config_values_.pointer_auto_focus_on](zx_koid_t koid) {
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
      inspect_node_.CreateChild("FocusManager"),
      /*legacy_focus_listener*/ [this](zx_koid_t old_focus, zx_koid_t new_focus) {
        engine_->scene_graph()->OnNewFocusedView(old_focus, new_focus);
      });
  scenic_->SetViewRefFocusedRegisterFunction(
      [this](zx_koid_t koid, fidl::InterfaceRequest<fuchsia::ui::views::ViewRefFocused> vrf) {
        focus_manager_->RegisterViewRefFocused(koid, std::move(vrf));
      });
  focus_manager_->Publish(*app_context_);
}

void App::InitializeHeartbeat(display::Display& display) {
  TRACE_DURATION("gfx", "App::InitializeHeartbeat");
  {  // Initialize ViewTreeSnapshotter

    // These callbacks are be called once per frame (at the end of OnCpuWorkDone()) and the results
    // used to build the ViewTreeSnapshot.
    // We create one per compositor.
    std::vector<view_tree::SubtreeSnapshotGenerator> subtrees_generator_callbacks;
    subtrees_generator_callbacks.emplace_back([this] {
      if (auto display = flatland_manager_->GetPrimaryFlatlandDisplayForRendering()) {
        return flatland_engine_->GenerateViewTreeSnapshot(display->root_transform());
      } else {
        return view_tree::SubtreeSnapshot{};  // Empty snapshot.
      }
    });
    // The i_can_haz_flatland flag is about eager-forcing of Flatland.
    // If true, then we KNOW that GFX should *not* run. Workstation is true.
    // if false, then either system could legitimately run. This flag is false for tests and
    // GFX-based products.
    if (!config_values_.i_can_haz_flatland) {
      subtrees_generator_callbacks.emplace_back(
          [this] { return engine_->scene_graph()->view_tree().Snapshot(); });
    }

    // All subscriber callbacks get called with the new snapshot every time one is generated (once
    // per frame).
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

    subscribers.push_back({.on_new_view_tree =
                               [this](auto snapshot) {
                                 geometry_provider_->OnNewViewTreeSnapshot(std::move(snapshot));
                               },
                           .dispatcher = async_get_default_dispatcher()});

    if (enable_snapshot_dump_) {
      subscribers.push_back({.on_new_view_tree =
                                 [](auto snapshot) {
                                   view_tree::SnapshotDump::OnNewViewTreeSnapshot(
                                       std::move(snapshot));
                                 },
                             .dispatcher = async_get_default_dispatcher()});
    }

    view_tree_snapshotter_ = std::make_shared<view_tree::ViewTreeSnapshotter>(
        std::move(subtrees_generator_callbacks), std::move(subscribers));
  }

  // |session_updaters| will be updated in submission order.
  frame_scheduler_.Initialize(
      /*vsync_timing*/ display.vsync_timing(),
      /*frame_renderer*/ frame_renderer_,
      /*session_updaters*/
      {scenic_, image_pipe_updater_, flatland_manager_, screen_capture2_manager_,
       flatland_presenter_, view_tree_snapshotter_});
}

}  // namespace scenic_impl
