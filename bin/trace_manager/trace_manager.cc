// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "apps/tracing/lib/trace/internal/types.h"
#include "apps/tracing/src/trace_manager/trace_manager.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"

namespace tracing {
namespace {

static constexpr size_t kSharedBufferSize = 3 * 1024 * 1024;
static const ftl::TimeDelta kStopTimeout = ftl::TimeDelta::FromSeconds(5);

bool IsAnyKnownCategoryEnabled(
    const fidl::Map<fidl::String, fidl::String>& known,
    const fidl::Array<fidl::String>& enabled) {
  // We treat the empty set of enabled or known categories as
  // a wildcard to turn on all categories.
  if (enabled.size() == 0 || known.size() == 0)
    return true;

  for (size_t i = 0; i < enabled.size(); i++)
    if (known.find(enabled[i]) != known.cend())
      return true;
  return false;
}

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
  static const size_t kMaxRecordSize = (1 << 12) - 1;

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
    auto length = internal::RecordFields::RecordSize::Get<uint16_t>(*current);
    if (length == 0 || length > kMaxRecordSize) {
      FTL_LOG(INFO) << "Invalid record length " << length;
      break;
    }
    current += length;
  }

  WriteBufferToSocket(buffer.data(), (current - start) * sizeof(uint64_t),
                      socket);
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

void TraceManager::RegisterTraceProvider(
    fidl::InterfaceHandle<TraceProvider> handle,
    const fidl::String& label,
    fidl::Map<fidl::String, fidl::String> categories) {
  FTL_LOG(INFO) << "Registering provider with label: " << label;
  providers_.emplace_back(TraceProviderPtr::Create(std::move(handle)),
                          std::move(label), std::move(categories));

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

  if (!IsAnyKnownCategoryEnabled(info->known_categories, categories_)) {
    FTL_LOG(INFO) << "Not starting provider " << info->label << ": "
                  << "No known category has been enabled";
    return false;
  }

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
