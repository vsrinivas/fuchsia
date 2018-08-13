// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <iostream>

#include <lib/zx/time.h>

#include "garnet/bin/trace_manager/trace_manager.h"
#include "lib/fidl/cpp/clone.h"

namespace tracing {
namespace {

// For large traces or when verbosity is on it can take awhile to write out
// all the records. E.g., ipm_provider can take 40 seconds with --verbose=2
constexpr zx::duration kStopTimeout = zx::sec(60);
static constexpr uint32_t kMinBufferSizeMegabytes = 1;
static constexpr uint32_t kMaxBufferSizeMegabytes = 64;

}  // namespace

TraceManager::TraceManager(component::StartupContext* context,
                           const Config& config)
    : context_(context), config_(config) {
  // TODO(jeffbrown): We should do this in StartTracing() and take care
  // to restart any crashed providers.  We should also wait briefly to ensure
  // that these providers have registered themselves before replying that
  // tracing has started.
  LaunchConfiguredProviders();
}

TraceManager::~TraceManager() = default;

void TraceManager::StartTracing(fuchsia::tracing::TraceOptions options,
                                zx::socket output,
                                StartTracingCallback start_callback) {
  if (session_) {
    FXL_LOG(ERROR) << "Trace already in progress";
    return;
  }

  uint32_t buffer_size_megabytes = std::min(
      std::max(options.buffer_size_megabytes_hint, kMinBufferSizeMegabytes),
      kMaxBufferSizeMegabytes);

  fuchsia::tracelink::BufferingMode tracelink_buffering_mode;
  const char* mode_name;
  switch (options.buffering_mode) {
    case fuchsia::tracing::BufferingMode::ONESHOT:
      tracelink_buffering_mode = fuchsia::tracelink::BufferingMode::ONESHOT;
      mode_name = "oneshot";
      break;
    case fuchsia::tracing::BufferingMode::CIRCULAR:
      tracelink_buffering_mode = fuchsia::tracelink::BufferingMode::CIRCULAR;
      mode_name = "circular";
      break;
    case fuchsia::tracing::BufferingMode::STREAMING:
      tracelink_buffering_mode = fuchsia::tracelink::BufferingMode::STREAMING;
      mode_name = "streaming";
      break;
    default:
      FXL_LOG(ERROR) << "Invalid buffering mode: "
                     << static_cast<unsigned>(options.buffering_mode);
      return;
  }

  FXL_LOG(INFO) << "Starting trace with " << buffer_size_megabytes
                << " MB buffers, buffering mode=" << mode_name;

  session_ = fxl::MakeRefCounted<TraceSession>(
      std::move(output), std::move(options.categories),
      buffer_size_megabytes * 1024 * 1024, tracelink_buffering_mode,
      [this]() { session_ = nullptr; });

  for (auto& bundle : providers_) {
    session_->AddProvider(&bundle);
  }

  session_->WaitForProvidersToStart(
      std::move(start_callback), zx::msec(options.start_timeout_milliseconds));
}

void TraceManager::StopTracing() {
  if (!session_)
    return;

  FXL_LOG(INFO) << "Stopping trace";
  session_->Stop(
      [this]() {
        FXL_LOG(INFO) << "Stopped trace";
        session_ = nullptr;
      },
      kStopTimeout);
}

void TraceManager::GetKnownCategories(GetKnownCategoriesCallback callback) {
  fidl::VectorPtr<fuchsia::tracing::KnownCategory> known_categories;
  for (const auto& it : config_.known_categories()) {
    known_categories.push_back(
        fuchsia::tracing::KnownCategory{it.first, it.second});
  }
  callback(std::move(known_categories));
}

void TraceManager::RegisterTraceProvider(
    fidl::InterfaceHandle<fuchsia::tracelink::Provider> handle) {
  auto it = providers_.emplace(
      providers_.end(),
      TraceProviderBundle{handle.Bind(), next_provider_id_++});

  it->provider.set_error_handler([this, it]() {
    if (session_)
      session_->RemoveDeadProvider(&(*it));
    providers_.erase(it);
  });

  if (session_)
    session_->AddProvider(&(*it));
}

void TraceManager::LaunchConfiguredProviders() {
  if (config_.providers().empty())
    return;

  if (!context_->launcher()) {
    FXL_LOG(ERROR)
        << "Cannot access application launcher to launch configured providers";
    return;
  }

  for (const auto& pair : config_.providers()) {
    // TODO(jeffbrown): Only do this if the provider isn't already running.
    // Also keep track of the provider so we can kill it when the trace
    // manager exits or restart it if needed.
    FXL_VLOG(1) << "Starting configured provider: " << pair.first;
    FXL_VLOG(2) << "URL: " << pair.second->url;
    if (FXL_VLOG_IS_ON(2)) {
      std::string args;
      for (const auto& arg : *pair.second->arguments) {
        args += " ";
        args += arg;
      }
      FXL_VLOG(2) << "Args:" << args;
    }
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = pair.second->url;
    fidl::Clone(pair.second->arguments, &launch_info.arguments);
    context_->launcher()->CreateComponent(std::move(launch_info), nullptr);
  }
}

}  // namespace tracing
