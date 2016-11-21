// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "apps/tracing/lib/trace/internal/fields.h"
#include "apps/tracing/src/trace_manager/trace_manager.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"

using namespace tracing::internal;

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

void WriteProviderInfoRecordToSocket(uint32_t provider_id,
                                     std::string provider_name,
                                     mx::socket& socket) {
  FTL_DCHECK(provider_name.size() <=
             ProviderInfoMetadataRecordFields::kMaxNameLength);

  size_t num_words = 1u + BytesToWords(Pad(provider_name.size()));
  std::vector<uint64_t> record(num_words);
  record[0] =
      ProviderInfoMetadataRecordFields::Type::Make(
          ToUnderlyingType(RecordType::kMetadata)) |
      ProviderInfoMetadataRecordFields::RecordSize::Make(num_words) |
      ProviderInfoMetadataRecordFields::MetadataType::Make(
          ToUnderlyingType(MetadataType::kProviderInfo)) |
      ProviderInfoMetadataRecordFields::Id::Make(provider_id) |
      ProviderInfoMetadataRecordFields::NameLength::Make(provider_name.size());
  memcpy(&record[1], provider_name.c_str(), provider_name.size());

  WriteBufferToSocket(reinterpret_cast<uint8_t*>(record.data()),
                      WordsToBytes(num_words), socket);
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
  const uint64_t* end = start + BytesToWords(buffer.size());

  while (current < end) {
    RecordHeader header = *current;
    auto length = RecordFields::RecordSize::Get<size_t>(header);
    auto type = RecordFields::RecordSize::Get<RecordType>(header);
    if (length == 0 || type == RecordType::kMetadata)
      break;  // end of stream or corrupt data
    FTL_DCHECK(length <= RecordFields::kMaxRecordSizeWords);
    current += length;
  }

  WriteBufferToSocket(buffer.data(), WordsToBytes(current - start), socket);
}

std::string SanitizeLabel(const fidl::String& label) {
  std::string result =
      label.get().substr(0, tracing::TraceRegistry::kLabelMaxLength);
  if (result.empty())
    result = "unnamed";
  return result;
}

}  // namespace

TraceManager::TraceManager() = default;

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

  providers_.emplace_back(TraceProviderPtr::Create(std::move(handle)), id,
                          std::move(sanitized_label));

  auto it = --providers_.end();
  it->provider.set_connection_error_handler(
      [ this, ptr = it->provider.get() ]() { EraseProvider(ptr); });
  FTL_VLOG(1) << "Registered trace provider: " << it->ToString();

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
  FTL_VLOG(2) << "Starting trace provider: " << info->ToString();

  if (mx::vmo::create(kSharedBufferSize, 0, &info->current_buffer) < 0) {
    FTL_LOG(ERROR) << "Failed to create shared buffer for provider: "
                   << info->ToString();
    return false;
  }

  mx::vmo first, second;
  if (info->current_buffer.duplicate(MX_RIGHT_SAME_RIGHTS, &first) < 0) {
    FTL_LOG(ERROR) << "Failed to dup shared buffer for provider: "
                   << info->ToString();
    return false;
  }

  info->provider->Start(std::move(first), std::move(second),
                        categories_.Clone());
  return true;
}

void TraceManager::StopTracingForProvider(ProviderInfo* info) {
  FTL_DCHECK(info->current_buffer);
  info->provider->Stop([this, info]() {
    FTL_VLOG(2) << "Trace provider stopped: " << info->ToString();

    WriteProviderInfoRecordToSocket(info->id, info->label, output_);
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

std::string TraceManager::ProviderInfo::ToString() {
  return ftl::StringPrintf("#%d '%s'", id, label.c_str());
}

}  // namespace tracing
