// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/buildinfo/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sysinfo/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>
#include <lib/zx/channel.h>
#include <lib/zx/clock.h>
#include <stdlib.h>
#include <zircon/boot/image.h>
#include <zircon/processargs.h>
#include <zircon/types.h>
#include <zircon/utc.h>

#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "lib/fidl/cpp/interface_request.h"
#include "src/cobalt/bin/app/cobalt_app.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/public/cobalt_config.h"

// Command-line flags

// Used to override kScheduleIntervalDefault;
constexpr std::string_view kScheduleIntervalSecondsFlagName = "schedule_interval_seconds";

constexpr std::string_view kInitialIntervalSecondsFlagName = "initial_interval_seconds";

// Used to override kMinIntervalDefault;
constexpr std::string_view kMinIntervalSecondsFlagName = "min_interval_seconds";

// Used to override kUploadJitterDefault;
constexpr std::string_view kUploadJitterFlagName = "upload_jitter";

// Used to override kEventAggregatorBackfillDaysDefault
constexpr std::string_view kEventAggregatorBackfillDaysFlagName = "event_aggregator_backfill_days";

// Used to override kStartEventAggregatorWorkerDefault
constexpr std::string_view kStartEventAggregatorWorkerFlagName = "start_event_aggregator_worker";

constexpr std::string_view kUseMemoryObservationStore = "use_memory_observation_store";

constexpr std::string_view kMaxBytesTotalFlagName = "max_bytes_per_observation_store";

constexpr std::string_view kPerProjectReservedBytes = "per_project_reserved_bytes";
constexpr std::string_view kTotalCapacityBytes = "local_storage_max_bytes_total";

constexpr std::string_view kTestOnlyFakeClockFlagName = "test_only_use_fake_clock";

constexpr std::string_view kRequireLifecycleService = "require_lifecycle_service";

constexpr std::string_view kTestDontBackfillEmptyReports = "test_dont_backfill_empty_reports";

// We want to only upload every hour. This is the interval that will be
// approached by the uploader.
const std::chrono::hours kScheduleIntervalDefault(1);

// We start uploading every minute and exponentially back off until we reach 1
// hour.
const std::chrono::minutes kInitialIntervalDefault(1);

// We send Observations to the Shuffler more frequently than kScheduleInterval
// under some circumstances, namely, if there is memory pressure or if we
// are explicitly asked to do so via the RequestSendSoon() method. This value
// is a safety parameter. We do not make two attempts within a period of this
// specified length.
const std::chrono::seconds kMinIntervalDefault(10);

// Offset uploads by at most 20% of current interval.
float kUploadJitterDefault(.2);

// The EventAggregator looks back 2 days, in addition to the previous day, to
// make sure that all locally aggregated observations have been generated.
const size_t kEventAggregatorBackfillDaysDefault(2);

// We normally start the EventAggregator's worker thread after constructing the
// EventAggregator.
constexpr bool kStartEventAggregatorWorkerDefault(true);

// ReadBoardName returns the board name of the currently running device.
//
// At the time of this writing, this will either be 'pc' for x86 devices, or an
// appropriate board name for ARM devices (sherlock, qemu).
//
// This uses the fuchsia-sysinfo fidl service to read the board_name field out
// of the ZBI. This string will never exceed a length of 32.
//
// If the reading of the board name fails for any reason, this will return "".
std::string ReadBoardName(const std::shared_ptr<sys::ServiceDirectory>& services) {
  fuchsia::sysinfo::SysInfoSyncPtr sysinfo;
  services->Connect<fuchsia::sysinfo::SysInfo>(sysinfo.NewRequest());

  // Read the board name out of the ZBI.
  zx_status_t status;
  fidl::StringPtr board_name;
  zx_status_t fidl_status = sysinfo->GetBoardName(&status, &board_name);
  if (fidl_status != ZX_OK || status != ZX_OK) {
    return "";
  }

  return board_name.value();
}

// Replaces the UTC clock installed in the process' namespace with a fake one
// that is always started. This is intended to simulate a synchronized clock in
// test environments without network and should not be used outside of tests.
void ReplaceRuntimeClock() {
  zx::clock current_clock(zx_utc_reference_get());
  zx::time start_time;
  current_clock.read(start_time.get_address());

  zx::clock replacement;
  zx_clock_create_args_v1_t clock_args{.backstop_time = start_time.get()};
  zx::clock::create(0u, &clock_args, &replacement);
  zx::clock::update_args update_args;
  update_args.set_value(start_time);
  replacement.update(update_args);
  zx_utc_reference_swap(replacement.release(), current_clock.reset_and_get_address());
}

int main(int argc, const char** argv) {
  // Parse the flags.
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line, {"cobalt", "fidl_service"});

  // Parse the schedule_interval_seconds flag.
  std::chrono::seconds schedule_interval =
      std::chrono::duration_cast<std::chrono::seconds>(kScheduleIntervalDefault);
  std::chrono::seconds initial_interval =
      std::chrono::duration_cast<std::chrono::seconds>(kInitialIntervalDefault);
  std::string flag_value;
  if (command_line.GetOptionValue(kScheduleIntervalSecondsFlagName, &flag_value)) {
    int num_seconds = std::stoi(flag_value);
    if (num_seconds > 0) {
      schedule_interval = std::chrono::seconds(num_seconds);
      // Set initial_interval, it can still be overridden by a flag.
      initial_interval = std::chrono::seconds(num_seconds);
    }
  }

  // Parse the initial_interval_seconds flag.
  flag_value.clear();
  if (command_line.GetOptionValue(kInitialIntervalSecondsFlagName, &flag_value)) {
    int num_seconds = std::stoi(flag_value);
    if (num_seconds > 0) {
      initial_interval = std::chrono::seconds(num_seconds);
    }
  }

  // Parse the min_interval_seconds flag.
  std::chrono::seconds min_interval =
      std::chrono::duration_cast<std::chrono::seconds>(kMinIntervalDefault);
  flag_value.clear();
  if (command_line.GetOptionValue(kMinIntervalSecondsFlagName, &flag_value)) {
    int num_seconds = std::stoi(flag_value);
    // We allow min_interval = 0.
    if (num_seconds >= 0) {
      min_interval = std::chrono::seconds(num_seconds);
    }
  }

  // Parse the upload_jitter flag.
  float upload_jitter = kUploadJitterDefault;
  flag_value.clear();
  if (command_line.GetOptionValue(kUploadJitterFlagName, &flag_value)) {
    float jitter_percentage = std::stof(flag_value);
    // We allow 0 <= jitter < 1.
    if (jitter_percentage >= 0 && jitter_percentage < 1) {
      upload_jitter = jitter_percentage;
    }
  }

  // Parse the event_aggregator_backfill_days flag.
  size_t event_aggregator_backfill_days = kEventAggregatorBackfillDaysDefault;
  flag_value.clear();
  if (command_line.GetOptionValue(kEventAggregatorBackfillDaysFlagName, &flag_value)) {
    int num_days = std::stoi(flag_value);
    // We allow num_days = 0.
    if (num_days >= 0) {
      event_aggregator_backfill_days = num_days;
    }
  }

  // Parse the start_event_aggregator_worker flag.
  bool start_event_aggregator_worker = kStartEventAggregatorWorkerDefault;
  flag_value.clear();
  if (command_line.GetOptionValue(kStartEventAggregatorWorkerFlagName, &flag_value)) {
    if (flag_value == "true") {
      start_event_aggregator_worker = true;
    } else if (flag_value == "false") {
      start_event_aggregator_worker = false;
    }
  }

  bool use_memory_observation_store = command_line.HasOption(kUseMemoryObservationStore);

  // TODO(fxbug.dev/94259): Update disk usage quotas post F7 cut.
  // Parse the max_bytes_per_observation_store
  size_t max_bytes_per_observation_store = size_t{460} * 1024;  // 460 KiB (~45% of 1MiB)
  flag_value.clear();
  if (command_line.GetOptionValue(kMaxBytesTotalFlagName, &flag_value)) {
    int num_bytes = std::stoi(flag_value);
    if (num_bytes > 0) {
      max_bytes_per_observation_store = num_bytes;
    }
  }

  // Parse StorageQuotas
  cobalt::StorageQuotas storage_quotas = {
      .per_project_reserved_bytes = 1024,          // 1KiB per project (enough for 460 projects)
      .total_capacity_bytes = int64_t{460} * 1024  // 460KiB for local aggregation (~45% of 1MiB)
  };
  flag_value.clear();
  if (command_line.GetOptionValue(kPerProjectReservedBytes, &flag_value)) {
    int num_bytes = std::stoi(flag_value);
    if (num_bytes > 0) {
      storage_quotas.per_project_reserved_bytes = num_bytes;
    }
  }
  flag_value.clear();
  if (command_line.GetOptionValue(kTotalCapacityBytes, &flag_value)) {
    int num_bytes = std::stoi(flag_value);
    if (num_bytes > 0) {
      storage_quotas.total_capacity_bytes = num_bytes;
    }
  }

  bool use_fake_clock = command_line.HasOption(kTestOnlyFakeClockFlagName);

  bool test_dont_backfill_empty_reports = command_line.HasOption(kTestDontBackfillEmptyReports);

  FX_LOGS(INFO) << "Cobalt is starting with the following parameters: "
                << "schedule_interval=" << schedule_interval.count()
                << " seconds, min_interval=" << min_interval.count()
                << " seconds, initial_interval=" << initial_interval.count()
                << " seconds, upload_jitter=" << (upload_jitter * 100) << "%"
                << ", max_bytes_per_observation_store=" << max_bytes_per_observation_store
                << ", storage_quotas.per_project_reserved_bytes="
                << storage_quotas.per_project_reserved_bytes
                << ", storage_quotas.total_capacity_bytes=" << storage_quotas.total_capacity_bytes
                << ", event_aggregator_backfill_days=" << event_aggregator_backfill_days
                << ", start_event_aggregator_worker=" << start_event_aggregator_worker
                << ", test_only_use_fake_clock=" << use_fake_clock
                << ", test_dont_backfill_empty_reports=" << test_dont_backfill_empty_reports << ".";

  if (use_fake_clock) {
    FX_LOGS(WARNING) << "Using a fake clock. This should only be enabled in tests.";
    ReplaceRuntimeClock();
  }

  if (test_dont_backfill_empty_reports) {
    FX_LOGS(WARNING) << "Not backfilling empty reports. This should only be enabled in tests.";
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  sys::ComponentInspector inspector(context.get());
  inspector.Health().StartingUp();

  // Lower the priority of the main thread.
  fuchsia::scheduler::ProfileProviderSyncPtr provider;
  context->svc()->Connect<fuchsia::scheduler::ProfileProvider>(provider.NewRequest());
  zx_status_t fidl_status;
  zx::profile profile;
  auto status =
      provider->GetProfile(0 /* LOWEST_PRIORITY */, "src/cobalt/bin/app", &fidl_status, &profile);
  if (status == ZX_OK) {
    FX_CHECK(fidl_status == ZX_OK) << "Recieved error while acquiring profile";
    FX_LOGS(INFO) << "Fetched profile!";
    status = zx_object_set_profile(zx_thread_self(), profile.get(), 0);
    FX_CHECK(status == ZX_OK) << "Unable to set current thread priority";
    FX_LOGS(INFO) << "Profile aplied to current thread";
  } else {
    // If we fail with ZX_ERR_PEER_CLOSED, then the ProfileProvider is down, and we should continue
    // anyway.
    FX_CHECK(status == ZX_ERR_PEER_CLOSED)
        << "Unable to connect to ProfileProvider, received unexpected error (" << status << ")";
    FX_LOGS(INFO) << "Unable to lower current thread provider. ProfileProvider is down.";
  }

  std::string product = "<product read failed>";
  std::string version = "<version read failed>";
  {
    fuchsia::buildinfo::ProviderSyncPtr buildinfo_provider;
    context->svc()->Connect<fuchsia::buildinfo::Provider>(buildinfo_provider.NewRequest());
    fuchsia::buildinfo::BuildInfo build_info;
    status = buildinfo_provider->GetBuildInfo(&build_info);
    if (status == ZX_OK) {
      if (!build_info.has_product_config() || build_info.product_config().empty()) {
        product = "<product not specified>";
      } else {
        product = build_info.product_config();
      }

      if (!build_info.has_version() || build_info.version().empty()) {
        version = "<version not specified>";
      } else {
        version = build_info.version();
      }
    }
  }

  bool require_lifecycle_service = command_line.HasOption(kRequireLifecycleService);
  fidl::InterfaceRequest<fuchsia::process::lifecycle::Lifecycle> lifecycle_handle;
  // Fetch the provided fuchsia::process::lifecycle::Lifecycle service handle.
  zx::channel lifecycle_request(zx_take_startup_handle(PA_LIFECYCLE));
  if (lifecycle_request.is_valid()) {
    lifecycle_handle.set_channel(std::move(lifecycle_request));
  } else {
    FX_LOGS(ERROR) << "Startup Error: Received invalid lifecycle handle. Cobalt will not be able "
                      "to listen for lifecycle events and shut down gracefully.";
    if (require_lifecycle_service) {
      FX_LOGS(FATAL) << "Lifecycle service is required. Exiting";
    }
  }

  auto boardname = ReadBoardName(context->svc());
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher(), "cobalt_fidl_provider");
  cobalt::UploadScheduleConfig upload_schedule = {
      .target_interval = schedule_interval,
      .min_interval = min_interval,
      .initial_interval = initial_interval,
      .jitter = upload_jitter,
  };

  cobalt::lib::statusor::StatusOr<std::unique_ptr<cobalt::CobaltApp>> app =
      cobalt::CobaltApp::CreateCobaltApp(
          std::move(context), loop.dispatcher(), std::move(lifecycle_handle),
          [loop = &loop]() { loop->Quit(); }, inspector.root().CreateChild("cobalt_app"),
          upload_schedule, event_aggregator_backfill_days, start_event_aggregator_worker,
          test_dont_backfill_empty_reports, use_memory_observation_store,
          max_bytes_per_observation_store, storage_quotas, product, boardname, version);

  if (!app.ok()) {
    FX_LOGS(FATAL) << "Failed to construct the cobalt app: " << app.status();
  }
  inspector.Health().Ok();
  loop.Run();
  FX_LOGS(INFO) << "Cobalt will now shut down.";
  return 0;
}
