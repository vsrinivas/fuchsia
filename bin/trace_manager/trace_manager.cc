// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <iostream>

#include "garnet/bin/trace_manager/trace_manager.h"
#include "lib/fsl/tasks/message_loop.h"

using namespace tracing::internal;

namespace tracing {
namespace {

const fxl::TimeDelta kStopTimeout = fxl::TimeDelta::FromSeconds(20);
static constexpr uint32_t kMinBufferSizeMegabytes = 1;
static constexpr uint32_t kMaxBufferSizeMegabytes = 32;

std::string SanitizeLabel(const fidl::String& label) {
  std::string result =
      label.get().substr(0, tracing::TraceRegistry::kLabelMaxLength);
  if (result.empty())
    result = "unnamed";
  return result;
}

}  // namespace

TraceManager::TraceManager(app::ApplicationContext* context,
                           const Config& config)
    : context_(context), config_(config) {
  // TODO(jeffbrown): We should do this in StartTracing() and take care
  // to restart any crashed providers.  We should also wait briefly to ensure
  // that these providers have registered themselves before replying that
  // tracing has started.
  LaunchConfiguredProviders();
}

TraceManager::~TraceManager() = default;

void TraceManager::StartTracing(TraceOptionsPtr options,
                                zx::socket output,
                                const StartTracingCallback& start_callback) {
  if (session_) {
    FXL_LOG(ERROR) << "Trace already in progress";
    return;
  }

  uint32_t buffer_size_megabytes = std::min(
      std::max(options->buffer_size_megabytes_hint, kMinBufferSizeMegabytes),
      kMaxBufferSizeMegabytes);
  FXL_LOG(INFO) << "Starting trace with " << buffer_size_megabytes
                << " MB buffers";

  session_ = fxl::MakeRefCounted<TraceSession>(
      std::move(output), std::move(options->categories),
      buffer_size_megabytes * 1024 * 1024, [this]() { session_ = nullptr; });

  for (auto& bundle : providers_) {
    FXL_VLOG(1) << "  for provider " << bundle;
    session_->AddProvider(&bundle);
  }

  session_->WaitForProvidersToStart(
      start_callback,
      fxl::TimeDelta::FromMilliseconds(options->start_timeout_milliseconds));
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

void TraceManager::DumpProvider(uint32_t provider_id, zx::socket output) {
  for (const auto& provider : providers_) {
    if (provider.id == provider_id) {
      FXL_LOG(INFO) << "Dumping provider: " << provider;
      provider.provider->Dump(std::move(output));
      return;
    }
  }
  FXL_LOG(ERROR) << "Failed to dump provider " << provider_id
                 << ", provider not found";
}

void TraceManager::GetKnownCategories(
    const GetKnownCategoriesCallback& callback) {
  callback(
      fidl::Map<fidl::String, fidl::String>::From(config_.known_categories()));
}

void TraceManager::GetRegisteredProviders(
    const GetRegisteredProvidersCallback& callback) {
  fidl::Array<TraceProviderInfoPtr> results;
  results.resize(0u);
  for (const auto& provider : providers_) {
    auto info = TraceProviderInfo::New();
    info->label = provider.label;
    info->id = provider.id;
    results.push_back(std::move(info));
  }
  callback(std::move(results));
}

void TraceManager::RegisterTraceProvider(
    fidl::InterfaceHandle<TraceProvider> handle,
    const fidl::String& label) {
  FXL_VLOG(1) << "Registering provider with label: " << label;

  auto it = providers_.emplace(
      providers_.end(),
      TraceProviderBundle{TraceProviderPtr::Create(std::move(handle)),
                          next_provider_id_++, SanitizeLabel(label)});

  it->provider.set_connection_error_handler([this, it]() {
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
    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = pair.second->url;
    launch_info->arguments = pair.second->arguments.Clone();
    context_->launcher()->CreateApplication(std::move(launch_info), nullptr);
  }
}

}  // namespace tracing
