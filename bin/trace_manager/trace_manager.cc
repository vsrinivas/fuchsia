// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "apps/tracing/lib/trace/internal/fields.h"
#include "apps/tracing/src/trace_manager/trace_manager.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"

namespace tracing {
namespace {

static constexpr size_t kSharedBufferSize = 3 * 1024 * 1024;
static const ftl::TimeDelta kStopTimeout = ftl::TimeDelta::FromSeconds(5);

void WriteBufferToSocket(const uint8_t* buffer,
                         size_t len,
                         mx::socket& socket) {
  mx_status_t status = NO_ERROR;
  mx_size_t actual = 0;
  mx_size_t offset = 0;

  while (offset < len) {
    if ((status = socket.write(0u, buffer + offset, len - offset, &actual)) <
        0) {
      if (status == ERR_SHOULD_WAIT) {
        mx_signals_t pending = 0;
        status = socket.wait_one(MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED,
                                 MX_TIME_INFINITE, &pending);
        if (status < 0)
          return;

        if (pending & MX_SIGNAL_WRITABLE)
          continue;

        if (pending & MX_SIGNAL_PEER_CLOSED)
          return;
      }
      return;
    }

    offset += actual;
  }
}

void WriteRecordsToSocket(mx::vmo vmo, size_t vmo_size, mx::socket& socket) {
  FTL_DCHECK(vmo);
  FTL_DCHECK(socket);

  std::vector<uint8_t> buffer(vmo_size);

  mx_status_t status = NO_ERROR;
  mx_size_t size = vmo_size;
  mx_size_t actual = 0;
  mx_size_t offset = 0;

  while (offset < size) {
    if ((status = vmo.read(buffer.data() + offset, offset, size - offset,
                           &actual)) != NO_ERROR) {
      FTL_LOG(ERROR) << "Failed to read buffer content: " << status;
      return;
    }
    offset += actual;
  }

  const uint64_t* start = reinterpret_cast<const uint64_t*>(buffer.data());
  const uint64_t* current = start;
  const uint64_t* end = start + (buffer.size() / sizeof(uint64_t));

  while (current < end) {
    auto length = internal::RecordFields::RecordSize::Get<size_t>(*current);
    if (length == 0)
      break;  // end of stream or corrupt data
    FTL_DCHECK(length <= internal::RecordFields::kMaxRecordSizeWords);
    current += length;
  }

  WriteBufferToSocket(buffer.data(), (current - start) * sizeof(uint64_t),
                      socket);
}

std::string SanitizeLabel(const fidl::String& label) {
  std::string result =
      label.get().substr(0, tracing::TraceRegistry::kLabelMaxLength);
  if (result.empty())
    result = "unnamed";
  return result;
}

}  // namespace

TraceManager::TraceManager() {
  FTL_LOG(INFO) << "Creating new instance";
}
TraceManager::~TraceManager() = default;

void TraceManager::StartTracing(fidl::Array<fidl::String> categories,
                                mx::socket output) {
  if (controller_state_ != ControllerState::kStopped)
    return;

  output_ = std::move(output);
  controller_state_ = ControllerState::kStarted;
  categories_ = std::move(categories);

  for (auto it = providers_.begin(); it != providers_.end(); ++it) {
    if (StartTracingForProvider(&*it))
      active_providers_.push_back(it);
  }

  ++generation_;
}

void TraceManager::StopTracing() {
  if (controller_state_ != ControllerState::kStarted)
    return;

  for (auto it : active_providers_)
    StopTracingForProvider(&*it);

  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [ this, generation = generation_ ]() {
        if (generation == generation_)
          FinalizeTracing();
      },
      kStopTimeout);
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
  const uint32_t id = next_provider_id_++;
  std::string sanitized_label = SanitizeLabel(label);

  FTL_LOG(INFO) << "Registering provider: id=" << id
                << ", label=" << sanitized_label;
  providers_.emplace_back(TraceProviderPtr::Create(std::move(handle)), id,
                          std::move(sanitized_label));

  auto it = --providers_.end();
  it->provider.set_connection_error_handler(
      [ this, ptr = it->provider.get() ]() { EraseProvider(ptr); });

  if (controller_state_ == ControllerState::kStarted) {
    if (StartTracingForProvider(&*it))
      active_providers_.push_back(it);
  }
}

void TraceManager::FinalizeTracing() {
  output_.reset();
  controller_state_ = ControllerState::kStopped;
}

bool TraceManager::StartTracingForProvider(ProviderInfo* info) {
  FTL_DCHECK(!info->current_buffer);
  FTL_LOG(INFO) << "StartTracingForProvider: " << info->label;

  if (mx::vmo::create(kSharedBufferSize, 0, &info->current_buffer) < 0) {
    FTL_LOG(ERROR) << "Failed to create shared buffer for provider";
    return false;
  }

  mx::vmo first, second;
  if (info->current_buffer.duplicate(MX_RIGHT_SAME_RIGHTS, &first) < 0) {
    FTL_LOG(ERROR) << "Failed to dup shared buffer for provider";
    return false;
  }

  info->provider->Start(std::move(first), std::move(second),
                        categories_.Clone());
  FTL_LOG(INFO) << "Started tracing for provider " << info->label;
  return true;
}

void TraceManager::StopTracingForProvider(ProviderInfo* info) {
  FTL_DCHECK(info->current_buffer);
  info->provider->Stop([this, info]() {
    FTL_LOG(INFO) << "Provider stopped, write out data";
    WriteRecordsToSocket(std::move(info->current_buffer), kSharedBufferSize,
                         output_);
    auto it = std::find_if(active_providers_.begin(), active_providers_.end(),
                           [info](auto it) { return &*it == info; });
    if (it != active_providers_.end())
      active_providers_.erase(it);
    if (active_providers_.empty())
      FinalizeTracing();
  });
}

void TraceManager::EraseProvider(TraceProvider* provider) {
  auto it = std::find_if(
      providers_.begin(), providers_.end(),
      [provider](const auto& info) { return info.provider.get() == provider; });
  FTL_DCHECK(it != providers_.end());
  if (std::find(active_providers_.begin(), active_providers_.end(), it) !=
      active_providers_.end())
    StopTracingForProvider(&*it);
  providers_.erase(it);
}

}  // namespace tracing
