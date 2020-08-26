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
#include <utility>

#include "src/developer/forensics/crash_reports/config.h"
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

using fuchsia::feedback::CrashReport;
using fuchsia::feedback::Snapshot;

constexpr zx::duration kChannelOrDeviceIdTimeout = zx::sec(30);
constexpr zx::duration kSnapshotTimeout = zx::min(2);

// Delta between snapshot requests that can be pooled together.
//
// We don't want it to be too high as data would get stale, e.g., logs, and we don't want it to be
// too low as we wouldn't benefit as much from pooling.
constexpr zx::duration kSnapshotPoolingDelta = zx::sec(5);

}  // namespace

std::unique_ptr<CrashReporter> CrashReporter::TryCreate(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    const timekeeper::Clock& clock, std::shared_ptr<InfoContext> info_context, const Config* config,
    const ErrorOr<std::string>& build_version, CrashRegister* crash_register) {
  std::unique_ptr<CrashServer> crash_server;
  if (config->crash_server.url) {
    crash_server = std::make_unique<CrashServer>(*(config->crash_server.url));
  }

  return std::unique_ptr<CrashReporter>(
      new CrashReporter(dispatcher, std::move(services), clock, std::move(info_context),
                        std::move(config), build_version, crash_register, std::move(crash_server)));
}

CrashReporter::CrashReporter(async_dispatcher_t* dispatcher,
                             std::shared_ptr<sys::ServiceDirectory> services,
                             const timekeeper::Clock& clock,
                             std::shared_ptr<InfoContext> info_context, const Config* config,
                             const ErrorOr<std::string>& build_version,
                             CrashRegister* crash_register,
                             std::unique_ptr<CrashServer> crash_server)
    : dispatcher_(dispatcher),
      executor_(dispatcher),
      services_(services),
      config_(std::move(config)),
      build_version_(build_version),
      crash_register_(crash_register),
      utc_provider_(services_, clock),
      crash_server_(std::move(crash_server)),
      queue_(dispatcher_, services_, info_context, crash_server_.get()),
      info_(info_context),
      privacy_settings_watcher_(dispatcher, services_, &settings_),
      data_provider_ptr_(dispatcher_, services_, kSnapshotPoolingDelta,
                         std::make_unique<timekeeper::SystemClock>()),
      device_id_provider_ptr_(dispatcher_, services_) {
  FX_CHECK(dispatcher_);
  FX_CHECK(services_);
  FX_CHECK(crash_register_);
  if (config->crash_server.url) {
    FX_CHECK(crash_server_);
  }

  const auto& upload_policy = config_->crash_server.upload_policy;
  settings_.set_upload_policy(upload_policy);
  if (upload_policy == CrashServerConfig::UploadPolicy::READ_FROM_PRIVACY_SETTINGS) {
    privacy_settings_watcher_.StartWatching();
  }

  queue_.WatchSettings(&settings_);

  info_.ExposeSettings(&settings_);
}

void CrashReporter::File(fuchsia::feedback::CrashReport report, FileCallback callback) {
  if (!report.has_program_name()) {
    FX_LOGS(ERROR) << "Input report missing required program name. Won't file.";
    callback(::fit::error(ZX_ERR_INVALID_ARGS));
    info_.LogCrashState(cobalt::CrashState::kDropped);
    return;
  }
  const std::string program_name = report.program_name();
  FX_LOGS(INFO) << "Generating report for '" << program_name << "'";

  auto snapshot_promise = data_provider_ptr_.GetSnapshot(kSnapshotTimeout);
  auto device_id_promise = device_id_provider_ptr_.GetId(kChannelOrDeviceIdTimeout);
  auto product_promise =
      crash_register_->GetProduct(program_name, fit::Timeout(kChannelOrDeviceIdTimeout));

  auto promise =
      ::fit::join_promises(std::move(snapshot_promise), std::move(device_id_promise),
                           std::move(product_promise))
          .then(
              [this, report = std::move(report), program_name](
                  ::fit::result<
                      std::tuple<::fit::result<Snapshot, Error>, ::fit::result<std::string, Error>,
                                 ::fit::result<Product>>>& results) mutable -> ::fit::result<void> {
                if (results.is_error()) {
                  return ::fit::error();
                }

                auto snapshot = std::move(std::get<0>(results.value()));
                auto device_id = std::move(std::get<1>(results.value()));
                auto product = std::move(std::get<2>(results.value()));

                if (product.is_error()) {
                  return ::fit::error();
                }

                std::optional<Report> final_report =
                    MakeReport(std::move(report), std::move(snapshot), utc_provider_.CurrentTime(),
                               device_id, build_version_, product.value());
                if (!final_report.has_value()) {
                  FX_LOGS(ERROR) << "Error generating report";
                  return ::fit::error();
                }

                if (!queue_.Add(std::move(final_report.value()))) {
                  FX_LOGS(ERROR) << "Error adding new report to the queue";
                  return ::fit::error();
                }

                return ::fit::ok();
              })
          .then([this, callback = std::move(callback)](::fit::result<void>& result) {
            if (result.is_error()) {
              FX_LOGS(ERROR) << "Failed to file report. Won't retry.";
              info_.LogCrashState(cobalt::CrashState::kDropped);
              callback(::fit::error(ZX_ERR_INTERNAL));
            } else {
              info_.LogCrashState(cobalt::CrashState::kFiled);
              callback(::fit::ok());
            }
          });

  executor_.schedule_task(std::move(promise));
}

}  // namespace crash_reports
}  // namespace forensics
