// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/crash_reporter.h"

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>
#include <zircon/utc.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include "src/developer/forensics/crash_reports/config.h"
#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/crash_reports/crash_server.h"
#include "src/developer/forensics/crash_reports/product.h"
#include "src/developer/forensics/crash_reports/report.h"
#include "src/developer/forensics/crash_reports/report_util.h"
#include "src/developer/forensics/feedback/device_id_provider.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/lib/timekeeper/system_clock.h"

namespace forensics {
namespace crash_reports {
namespace {

using FidlSnapshot = fuchsia::feedback::Snapshot;
using fuchsia::feedback::CrashReport;

constexpr zx::duration kChannelOrDeviceIdTimeout = zx::sec(30);
constexpr zx::duration kSnapshotTimeout = zx::min(2);

// Returns what the initial ReportId should be, based on the contents of the store in the
// filesystem.
//
// Note: This function traverses store in the filesystem to and should be used sparingly.
ReportId SeedReportId() {
  // The next ReportId will be one more than the largest in the store.
  auto all_report_ids = StoreMetadata(kStoreTmpPath, kStoreMaxTmpSize).Reports();
  const auto all_cache_report_ids = StoreMetadata(kStoreCachePath, kStoreMaxCacheSize).Reports();
  all_report_ids.insert(all_report_ids.end(), all_cache_report_ids.begin(),
                        all_cache_report_ids.end());

  std::sort(all_report_ids.begin(), all_report_ids.end());
  return (all_report_ids.empty()) ? 0u : all_report_ids.back() + 1;
}

struct CrashReporterError {
  cobalt::CrashState crash_state;
  const char* log_message;
};

// Make the appropriate ReportingPolicyWatcher for the upload policy in |config|.
std::unique_ptr<ReportingPolicyWatcher> MakeReportingPolicyWatcher(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    const Config& config) {
  switch (config.crash_server.upload_policy) {
    case CrashServerConfig::UploadPolicy::ENABLED:
      // Uploads being enabled in |config| is explcit consent to upload all reports.
      return std::make_unique<StaticReportingPolicyWatcher<ReportingPolicy::kUpload>>();
    case CrashServerConfig::UploadPolicy::DISABLED:
      // Uploads being disabled in |config| means that reports should be archived.
      return std::make_unique<StaticReportingPolicyWatcher<ReportingPolicy::kArchive>>();
    case CrashServerConfig::UploadPolicy::READ_FROM_PRIVACY_SETTINGS:
      return std::make_unique<UserReportingPolicyWatcher>(dispatcher, std::move(services));
  }
}

}  // namespace

CrashReporter::CrashReporter(async_dispatcher_t* dispatcher,
                             const std::shared_ptr<sys::ServiceDirectory>& services,
                             timekeeper::Clock* clock,
                             const std::shared_ptr<InfoContext>& info_context, Config config,
                             AnnotationMap default_annotations, CrashRegister* crash_register,
                             LogTags* tags, SnapshotManager* snapshot_manager,
                             CrashServer* crash_server,
                             feedback::DeviceIdProvider* device_id_provider)
    : dispatcher_(dispatcher),
      executor_(dispatcher),
      services_(services),
      tags_(tags),
      default_annotations_(std::move(default_annotations)),
      crash_register_(crash_register),
      utc_provider_(dispatcher_, zx::unowned_clock(zx_utc_reference_get()), clock),
      snapshot_manager_(snapshot_manager),
      crash_server_(crash_server),
      queue_(dispatcher_, services_, info_context, tags_, crash_server_, snapshot_manager_),
      product_quotas_(dispatcher_, config.daily_per_product_quota),
      info_(info_context),
      network_watcher_(dispatcher_, *services_),
      reporting_policy_watcher_(MakeReportingPolicyWatcher(dispatcher_, services, config)),
      device_id_provider_(device_id_provider) {
  FX_CHECK(dispatcher_);
  FX_CHECK(services_);
  FX_CHECK(crash_register_);
  FX_CHECK(crash_server_);

  next_report_id_ = SeedReportId();

  queue_.WatchReportingPolicy(reporting_policy_watcher_.get());
  queue_.WatchNetwork(&network_watcher_);

  info_.ExposeReportingPolicy(reporting_policy_watcher_.get());

  if (config.hourly_snapshot) {
    // We schedule the first hourly snapshot in 5 minutes and then it will auto-schedule itself
    // every hour after that.
    ScheduleHourlySnapshot(zx::min(5));
  }
}

void CrashReporter::PersistAllCrashReports() {
  queue_.StopUploading();
  snapshot_manager_->Shutdown();
}

void CrashReporter::File(fuchsia::feedback::CrashReport report, FileCallback callback) {
  if (!report.has_program_name()) {
    FX_LOGS(ERROR) << "Input report missing required program name. Won't file.";
    callback(::fpromise::error(ZX_ERR_INVALID_ARGS));
    info_.LogCrashState(cobalt::CrashState::kDropped);
    return;
  }

  // Execute the callback informing the client the report has been filed. The rest of the async flow
  // can take quite some time and blocking clients would defeat the purpose of sharing the snapshot.
  callback(::fpromise::ok());

  File(std::move(report), /*is_hourly_snapshot=*/false);
};

void CrashReporter::File(fuchsia::feedback::CrashReport report, const bool is_hourly_snapshot) {
  if (reporting_policy_watcher_->CurrentPolicy() == ReportingPolicy::kDoNotFileAndDelete) {
    info_.LogCrashState(cobalt::CrashState::kDeleted);
    return;
  }

  const std::string program_name = report.program_name();
  const auto report_id = next_report_id_++;

  tags_->Register(report_id, {Logname(program_name)});

  using promise_tuple_t =
      std::tuple<::fpromise::result<SnapshotUuid>, ::fpromise::result<std::string, Error>,
                 ::fpromise::result<Product>>;

  auto promise =
      crash_register_->GetProduct(program_name, fit::Timeout(kChannelOrDeviceIdTimeout))
          .or_else([]() -> ::fpromise::result<Product, CrashReporterError> {
            return ::fpromise::error(
                CrashReporterError{cobalt::CrashState::kDropped, "failed GetProduct"});
          })
          .and_then([this, report_id, is_hourly_snapshot](Product& product)
                        -> ::fpromise::promise<promise_tuple_t, CrashReporterError> {
            if (!product_quotas_.HasQuotaRemaining(product)) {
              FX_LOGST(INFO, tags_->Get(report_id)) << "Daily report quota reached, won't retry";
              return ::fpromise::make_result_promise<promise_tuple_t, CrashReporterError>(
                  ::fpromise::error(CrashReporterError{
                      cobalt::CrashState::kOnDeviceQuotaReached,
                      nullptr,
                  }));
            }

            product_quotas_.DecrementRemainingQuota(product);

            auto snapshot_uuid_promise = snapshot_manager_->GetSnapshotUuid(kSnapshotTimeout);
            auto device_id_promise = device_id_provider_->GetId(kChannelOrDeviceIdTimeout);
            auto product_promise = ::fpromise::make_ok_promise(std::move(product));

            FX_LOGST(INFO, tags_->Get(report_id))
                << ((is_hourly_snapshot) ? "Generating hourly snapshot" : "Generating report");

            return ::fpromise::join_promises(std::move(snapshot_uuid_promise),
                                             std::move(device_id_promise),
                                             std::move(product_promise))
                .or_else([]() -> ::fpromise::result<promise_tuple_t, CrashReporterError> {
                  return ::fpromise::error(CrashReporterError{
                      cobalt::CrashState::kDropped,
                      "Failed join_promises()",
                  });
                });
          })
          .and_then([this, report = std::move(report), report_id,
                     is_hourly_snapshot](promise_tuple_t& results) mutable
                    -> ::fpromise::result<void, CrashReporterError> {
            auto snapshot_uuid = std::get<0>(results).take_value();
            auto device_id = std::move(std::get<1>(results));
            auto product = std::get<2>(results).take_value();

            std::optional<Report> final_report = MakeReport(
                std::move(report), report_id, snapshot_uuid,
                snapshot_manager_->GetSnapshot(snapshot_uuid), utc_provider_.CurrentTime(),
                device_id, default_annotations_, product, is_hourly_snapshot);
            if (!final_report.has_value()) {
              return ::fpromise::error(
                  CrashReporterError{cobalt::CrashState::kDropped, "failed MakeReport()"});
            }

            FX_LOGST(INFO, tags_->Get(report_id))
                << ((is_hourly_snapshot) ? "Generated hourly snapshot" : "Generated report");

            if (!queue_.Add(std::move(final_report.value()))) {
              return ::fpromise::error(
                  CrashReporterError{cobalt::CrashState::kDropped, "failed Queue::Add()"});
            }

            return ::fpromise::ok();
          })
          .then([this, report_id](::fpromise::result<void, CrashReporterError>& result) {
            if (result.is_error()) {
              if (result.error().log_message) {
                FX_LOGST(ERROR, tags_->Get(report_id))
                    << "Failed to file report: " << result.error().log_message << ". Won't retry";
              }
              tags_->Unregister(report_id);
              info_.LogCrashState(result.error().crash_state);
            } else {
              info_.LogCrashState(cobalt::CrashState::kFiled);
            }
          });

  executor_.schedule_task(std::move(promise));
}

void CrashReporter::ScheduleHourlySnapshot(const zx::duration delay) {
  async::PostDelayedTask(
      dispatcher_,
      [this]() {
        auto schedule_next = ::fit::defer([this] { ScheduleHourlySnapshot(zx::hour(1)); });

        if (queue_.HasHourlyReport()) {
          FX_LOGS(INFO) << "Skipping hourly snapshot as the last one has not been uploaded yet "
                           "– connectivity issues?";
          return;
        }

        fuchsia::feedback::CrashReport report;
        report.set_program_name(kHourlySnapshotProgramName)
            .set_program_uptime(zx_clock_get_monotonic())
            .set_is_fatal(false)
            .set_crash_signature(kHourlySnapshotSignature);

        File(std::move(report), /*is_hourly_snapshot=*/true);
      },
      delay);
}

}  // namespace crash_reports
}  // namespace forensics
