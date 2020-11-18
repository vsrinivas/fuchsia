// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/crash_reporter.h"

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fit/promise.h>
#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "src/developer/forensics/crash_reports/config.h"
#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/crash_reports/crash_server.h"
#include "src/developer/forensics/crash_reports/product.h"
#include "src/developer/forensics/crash_reports/report.h"
#include "src/developer/forensics/crash_reports/report_util.h"
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

// If a crash report arrives within |kSnapshotSharedRequestWindow| of a call to
// SnapshotManager::GetSnapshotUuid that schedules a call to
// fuchsia.feedback.DataProvider/GetSnapshot, the returned snapshot will be used in the resulting
// report.
//
// If the value it too large, crash reports may take too long to generate, but if the value is too
// small, the benefits of combining calls to fuchsia.feedback.DataProvider/GetSnapshot may not be
// fully realized.
constexpr zx::duration kSnapshotSharedRequestWindow = zx::sec(5);

// Returns what the initial ReportId should be, based on the contents of the store in the
// filesystem.
//
// Note: This function traverses store in the filesystem to and should be used sparingly.
ReportId SeedReportId() {
  // The next ReportId will be one more than the largest in the store.
  auto all_report_ids = StoreMetadata(kStorePath, kStoreMaxSize).Reports();
  std::sort(all_report_ids.begin(), all_report_ids.end());
  return (all_report_ids.empty()) ? 0u : all_report_ids.back() + 1;
}

struct CrashReporterError {
  cobalt::CrashState crash_state;
  std::string_view log_message;
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

std::unique_ptr<CrashReporter> CrashReporter::TryCreate(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    timekeeper::Clock* clock, std::shared_ptr<InfoContext> info_context, Config config,
    const ErrorOr<std::string>& build_version, CrashRegister* crash_register) {
  std::unique_ptr<SnapshotManager> snapshot_manager =
      std::make_unique<SnapshotManager>(dispatcher, services, clock, kSnapshotSharedRequestWindow,
                                        kSnapshotAnnotationsMaxSize, kSnapshotArchivesMaxSize);

  auto tags = std::make_unique<LogTags>();

  auto crash_server =
      std::make_unique<CrashServer>(services, kCrashServerUrl, snapshot_manager.get(), tags.get());

  return std::unique_ptr<CrashReporter>(
      new CrashReporter(dispatcher, std::move(services), clock, std::move(info_context),
                        std::move(config), build_version, crash_register, std::move(tags),
                        std::move(snapshot_manager), std::move(crash_server)));
}

CrashReporter::CrashReporter(async_dispatcher_t* dispatcher,
                             std::shared_ptr<sys::ServiceDirectory> services,
                             timekeeper::Clock* clock, std::shared_ptr<InfoContext> info_context,
                             Config config, const ErrorOr<std::string>& build_version,
                             CrashRegister* crash_register, std::unique_ptr<LogTags> tags,
                             std::unique_ptr<SnapshotManager> snapshot_manager,
                             std::unique_ptr<CrashServer> crash_server)
    : dispatcher_(dispatcher),
      executor_(dispatcher),
      services_(services),
      tags_(std::move(tags)),
      build_version_(build_version),
      crash_register_(crash_register),
      utc_provider_(services_, clock),
      snapshot_manager_(std::move(snapshot_manager)),
      crash_server_(std::move(crash_server)),
      queue_(dispatcher_, services_, info_context, tags_.get(), crash_server_.get(),
             snapshot_manager_.get()),
      product_quotas_(dispatcher_, config.daily_per_product_quota),
      info_(info_context),
      network_watcher_(dispatcher_, services_),
      reporting_policy_watcher_(MakeReportingPolicyWatcher(dispatcher_, services, config)),
      device_id_provider_ptr_(dispatcher_, services_) {
  FX_CHECK(dispatcher_);
  FX_CHECK(services_);
  FX_CHECK(crash_register_);
  FX_CHECK(crash_server_);

  next_report_id_ = SeedReportId();

  queue_.WatchReportingPolicy(reporting_policy_watcher_.get());
  queue_.WatchNetwork(&network_watcher_);

  info_.ExposeReportingPolicy(reporting_policy_watcher_.get());
}

void CrashReporter::File(fuchsia::feedback::CrashReport report, FileCallback callback) {
  if (!report.has_program_name()) {
    FX_LOGS(ERROR) << "Input report missing required program name. Won't file.";
    callback(::fit::error(ZX_ERR_INVALID_ARGS));
    info_.LogCrashState(cobalt::CrashState::kDropped);
    return;
  }

  if (reporting_policy_watcher_->CurrentPolicy() == ReportingPolicy::kDoNotFileAndDelete) {
    callback(::fit::ok());
    info_.LogCrashState(cobalt::CrashState::kDeleted);
    return;
  }

  // Execute the callback informing the client the report has been filed. The rest of the async flow
  // can take quite some time and blocking clients would defeat the purpose of sharing the snapshot.
  callback(::fit::ok());

  const std::string program_name = report.program_name();
  const auto report_id = next_report_id_++;

  tags_->Register(report_id, {Logname(program_name)});

  using promise_tuple_t = std::tuple<::fit::result<SnapshotUuid>, ::fit::result<std::string, Error>,
                                     ::fit::result<Product>>;

  auto promise =
      crash_register_->GetProduct(program_name, fit::Timeout(kChannelOrDeviceIdTimeout))
          .or_else([]() -> ::fit::result<Product, CrashReporterError> {
            return ::fit::error(
                CrashReporterError{cobalt::CrashState::kDropped, "failed GetProduct"});
          })
          .and_then([this, report_id](
                        Product& product) -> ::fit::promise<promise_tuple_t, CrashReporterError> {
            if (!product_quotas_.HasQuotaRemaining(product)) {
              return ::fit::make_result_promise<promise_tuple_t, CrashReporterError>(
                  ::fit::error(CrashReporterError{
                      cobalt::CrashState::kOnDeviceQuotaReached,
                      "daily report quota reached",
                  }));
            }

            product_quotas_.DecrementRemainingQuota(product);

            auto snapshot_uuid_promise = snapshot_manager_->GetSnapshotUuid(kSnapshotTimeout);
            auto device_id_promise = device_id_provider_ptr_.GetId(kChannelOrDeviceIdTimeout);
            auto product_promise = ::fit::make_ok_promise(std::move(product));

            FX_LOGST(INFO, tags_->Get(report_id)) << "Generating report";

            return ::fit::join_promises(std::move(snapshot_uuid_promise),
                                        std::move(device_id_promise), std::move(product_promise))
                .or_else([]() -> ::fit::result<promise_tuple_t, CrashReporterError> {
                  return ::fit::error(CrashReporterError{
                      cobalt::CrashState::kDropped,
                      "Failed join_promises()",
                  });
                });
          })
          .and_then([this, report = std::move(report), report_id](promise_tuple_t& results) mutable
                    -> ::fit::result<void, CrashReporterError> {
            auto snapshot_uuid = std::get<0>(results).take_value();
            auto device_id = std::move(std::get<1>(results));
            auto product = std::get<2>(results).take_value();

            std::optional<Report> final_report =
                MakeReport(std::move(report), report_id, snapshot_uuid, utc_provider_.CurrentTime(),
                           device_id, build_version_, product);
            if (!final_report.has_value()) {
              return ::fit::error(
                  CrashReporterError{cobalt::CrashState::kDropped, "failed MakeReport()"});
            }

            if (!queue_.Add(std::move(final_report.value()))) {
              return ::fit::error(
                  CrashReporterError{cobalt::CrashState::kDropped, "failed Queue::Add()"});
            }

            return ::fit::ok();
          })
          .then([this, report_id](::fit::result<void, CrashReporterError>& result) {
            if (result.is_error()) {
              FX_LOGST(ERROR, tags_->Get(report_id))
                  << "Failed to file report: " << result.error().log_message << ". Won't retry";
              tags_->Unregister(report_id);
              info_.LogCrashState(result.error().crash_state);
            } else {
              info_.LogCrashState(cobalt::CrashState::kFiled);
            }
          });

  executor_.schedule_task(std::move(promise));
}

}  // namespace crash_reports
}  // namespace forensics
