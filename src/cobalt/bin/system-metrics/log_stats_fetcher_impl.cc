// Copyright 2020  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/log_stats_fetcher_impl.h"

#include <lib/syslog/cpp/macros.h>

namespace cobalt {

namespace {

// A map from components urls in the approve-list to the corresponding Cobalt event
// codes (as defined in metrics.yaml).
const std::unordered_map<std::string, ComponentEventCode> kComponentCodeMap{
    {"fuchsia-boot:///#meta/driver_manager.cm", ComponentEventCode::DriverManager},
    {"fuchsia-pkg://fuchsia.com/a11y_manager#meta/a11y_manager.cmx",
     ComponentEventCode::A11yManager},
    {"fuchsia-pkg://fuchsia.com/audio_core_google#meta/audio_core.cmx",
     ComponentEventCode::AudioCore},
    {"fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm", ComponentEventCode::Appmgr},
    {"fuchsia-pkg://fuchsia.com/brightness_manager#meta/brightness_manager.cmx",
     ComponentEventCode::BrightnessManager},
    {"fuchsia-pkg://fuchsia.com/cast_agent#meta/cast_agent.cmx", ComponentEventCode::CastAgent},
    {"fuchsia-pkg://fuchsia.com/cast_runner#meta/cast_runner.cmx", ComponentEventCode::CastRunner},
    {"fuchsia-pkg://fuchsia.com/cobalt#meta/cobalt.cmx", ComponentEventCode::Cobalt},
    {"fuchsia-pkg://fuchsia.com/dhcpd#meta/dhcpd.cmx", ComponentEventCode::Dhcpd},
    {"fuchsia-pkg://fuchsia.com/hwinfo#meta/hwinfo.cmx", ComponentEventCode::Hwinfo},
    {"fuchsia-pkg://fuchsia.com/mdns#meta/mdns.cmx", ComponentEventCode::Mdns},
    {"fuchsia-pkg://fuchsia.com/netcfg#meta/netcfg.cmx", ComponentEventCode::Netcfg},
    {"fuchsia-pkg://fuchsia.com/pkg-resolver#meta/pkg-resolver.cmx",
     ComponentEventCode::PkgResolver},
    {"fuchsia-pkg://fuchsia.com/root_presenter#meta/root_presenter.cmx",
     ComponentEventCode::RootPresenter},
    {"fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx", ComponentEventCode::Scenic},
    {"fuchsia-pkg://fuchsia.com/sessionmgr#meta/sessionmgr.cmx", ComponentEventCode::SessionMgr},
    {"fuchsia-pkg://fuchsia.com/sysmgr#meta/sysmgr.cmx", ComponentEventCode::Sysmgr},
    {"fuchsia-pkg://fuchsia.com/system-update-checker#meta/system-update-checker.cmx",
     ComponentEventCode::SystemUpdateChecker},
    {"fuchsia-pkg://fuchsia.com/web_engine#meta/context_provider.cmx",
     ComponentEventCode::ContextProvider},
};

}  // namespace

LogStatsFetcherImpl::LogStatsFetcherImpl(async_dispatcher_t* dispatcher,
                                         sys::ComponentContext* context)
    : executor_(dispatcher),
      archive_reader_(context->svc()->Connect<fuchsia::diagnostics::ArchiveAccessor>(),
                      {"archivist.cmx:root/log_stats:error_logs",
                       "archivist.cmx:root/log_stats/by_component/*:error_logs"}) {
  // This establishes a baseline for error counts so that the next call to FetchMetrics() only
  // includes logs since the daemon started as opposed to since the  boot time. This is especially
  // important if the daemon had previously crashed and has been relaunched.
  FetchMetrics([](const Metrics& metrics) {});
}

void LogStatsFetcherImpl::FetchMetrics(MetricsCallback metrics_callback) {
  metrics_callback_ = std::move(metrics_callback);
  auto promise = archive_reader_.GetInspectSnapshot()
                     .and_then([this](std::vector<inspect::contrib::DiagnosticsData>& data_vector) {
                       this->OnInspectSnapshotReady(data_vector);
                     })
                     .or_else([](std::string& error) {
                       FX_LOGS(WARNING) << "Error while fetching log stats: " << error;
                     });
  executor_.schedule_task(std::move(promise));
}

void LogStatsFetcherImpl::OnInspectSnapshotReady(
    const std::vector<inspect::contrib::DiagnosticsData>& data_vector) {
  // Since we only asked for one component's inspect, there shouldn't be more
  // than one entry in the result.
  if (data_vector.size() != 1u) {
    FX_LOGS(ERROR) << "Expected 1 archive, received " << data_vector.size();
    return;
  }

  LogStatsFetcher::Metrics metrics;

  // Populate per-component log stats.
  const rapidjson::Value& component_list =
      data_vector[0].GetByPath({"root", "log_stats", "by_component"});
  if (!component_list.IsObject()) {
    FX_LOGS(ERROR) << "root/log_stats/by_component doesn't exist or is not an object";
    return;
  }
  for (auto& component_member : component_list.GetObject()) {
    std::string component_url = component_member.name.GetString();

    auto code_it = kComponentCodeMap.find(component_url);
    // Ignore components not in the whitelist.
    if (code_it == kComponentCodeMap.end())
      continue;

    ComponentEventCode component_code = code_it->second;

    if (!component_member.value.IsObject()) {
      FX_LOGS(ERROR) << "Component member must be an object: " << component_member.name.GetString();
      return;
    }

    const auto& component_object = component_member.value.GetObject();

    auto error_logs_it = component_object.FindMember("error_logs");
    if (error_logs_it == component_object.MemberEnd()) {
      FX_LOGS(ERROR) << component_url << " does not have error_logs";
      return;
    }

    if (!error_logs_it->value.IsUint64()) {
      FX_LOGS(ERROR) << "error_logs for component " << component_url << "is not uint64";
      return;
    }

    uint64_t new_count = error_logs_it->value.GetUint64();
    uint64_t last_count = per_component_error_count_[component_code];

    // If the new error count is lower, the component must have restarted, so
    // assume all errors are new.
    uint64_t diff = new_count < last_count ? new_count : new_count - last_count;
    if (diff > 0) {
      FX_LOGS(DEBUG) << "Found " << diff << " new error logs for component " << component_url
                     << ", total is " << new_count;
    }
    metrics.per_component_error_count[component_code] = diff;
    per_component_error_count_[component_code] = new_count;
  }

  // Find the total error count.
  const rapidjson::Value& new_count_value =
      data_vector[0].GetByPath({"root", "log_stats", "error_logs"});
  if (!new_count_value.IsUint64()) {
    FX_LOGS(ERROR) << "error_logs doesn't exist or is not a uint64";
    return;
  }
  uint64_t new_count = new_count_value.GetUint64();
  // If the new error count is lower, Archivist  must have restarted, so
  // assume all errors are new.
  if (new_count < last_reported_error_count_) {
    metrics.error_count = new_count;
  } else {
    metrics.error_count = new_count - last_reported_error_count_;
  }
  FX_LOGS(DEBUG) << "Current aggregated error count: " << new_count
                 << ", since last report: " << metrics.error_count;
  last_reported_error_count_ = new_count;

  metrics_callback_(metrics);
}

}  // namespace cobalt
