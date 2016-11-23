// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/internal/fields.h"
#include "apps/tracing/src/trace_manager/tracee.h"
#include "lib/ftl/logging.h"

namespace tracing {
namespace {

// Writes |len| bytes from |buffer| to |socket|.
// Returns false to indicate that no further operations
// on |socket| should happen, true otherwise.
bool WriteBufferToSocket(const uint8_t* buffer,
                         size_t len,
                         const mx::socket& socket) {
  mx_size_t offset = 0;
  while (offset < len) {
    mx_status_t status = NO_ERROR;
    mx_size_t actual = 0;
    if ((status = socket.write(0u, buffer + offset, len - offset, &actual)) <
        0) {
      if (status == ERR_SHOULD_WAIT) {
        mx_signals_t pending = 0;
        status = socket.wait_one(MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED,
                                 MX_TIME_INFINITE, &pending);
        if (status < 0) {
          FTL_LOG(ERROR) << "Wait on socket failed: " << status;
          return false;
        }

        if (pending & MX_SIGNAL_WRITABLE)
          continue;

        if (pending & MX_SIGNAL_PEER_CLOSED) {
          FTL_LOG(ERROR) << "Peer closed while writing to socket";
          return false;
        }
      }

      return false;
    }
    offset += actual;
  }

  return true;
}

}  // namespace

Tracee::Tracee(TraceProviderBundle* bundle) : bundle_(bundle) {}

Tracee::~Tracee() {
  if (fence_handler_key_) {
    mtl::MessageLoop::GetCurrent()->RemoveHandler(fence_handler_key_);
  }
}

bool Tracee::operator==(TraceProviderBundle* bundle) const {
  return bundle_ == bundle;
}

bool Tracee::Start(size_t buffer_size,
                   fidl::Array<fidl::String> categories,
                   ftl::Closure stop_callback) {
  FTL_DCHECK(!buffer_vmo_);
  FTL_DCHECK(stop_callback);

  mx::vmo buffer_vmo;
  mx_status_t status = mx::vmo::create(buffer_size, 0u, &buffer_vmo);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "Failed to create trace buffer: status=" << status;
    return false;
  }

  mx::vmo buffer_vmo_for_provider;
  status =
      buffer_vmo.duplicate(MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER |
                               MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_MAP,
                           &buffer_vmo_for_provider);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "Failed to duplicate trace buffer for provider: status="
                   << status;
    return false;
  }

  mx::eventpair fence, fence_for_provider;
  status = mx::eventpair::create(0u, &fence, &fence_for_provider);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "Failed to create trace buffer fence: status=" << status;
    return false;
  }

  bundle_->provider->Start(std::move(buffer_vmo_for_provider),
                           std::move(fence_for_provider),
                           std::move(categories));

  buffer_vmo_ = std::move(buffer_vmo);
  buffer_vmo_size_ = buffer_size;
  fence_ = std::move(fence);
  stop_callback_ = std::move(stop_callback);
  fence_handler_key_ = mtl::MessageLoop::GetCurrent()->AddHandler(
      this, fence_.get(), MX_EPAIR_CLOSED);
  return true;
}

void Tracee::Stop() {
  bundle_->provider->Stop();
}

void Tracee::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  FTL_DCHECK(pending & MX_EPAIR_CLOSED);
  FTL_DCHECK(stop_callback_);

  mtl::MessageLoop::GetCurrent()->RemoveHandler(fence_handler_key_);
  fence_handler_key_ = 0u;

  ftl::Closure stop_callback = std::move(stop_callback_);
  stop_callback();
}

void Tracee::OnHandleError(mx_handle_t handle, mx_status_t error) {
  FTL_DCHECK(error == ERR_BAD_STATE);
}

bool Tracee::WriteRecords(const mx::socket& socket) const {
  FTL_DCHECK(socket);
  FTL_DCHECK(buffer_vmo_);

  if (!WriteProviderInfoRecord(socket)) {
    FTL_LOG(ERROR) << "Failed to write provider info record to trace.";
    return false;
  }

  std::vector<uint8_t> buffer(buffer_vmo_size_);

  mx_size_t actual = 0;
  if ((buffer_vmo_.read(buffer.data(), 0, buffer_vmo_size_, &actual) !=
       NO_ERROR) ||
      (actual != buffer_vmo_size_)) {
    FTL_LOG(WARNING) << "Failed to read data from buffer_vmo: "
                     << "actual size=" << actual
                     << ", expected size=" << buffer_vmo_size_;
  }

  const uint64_t* start = reinterpret_cast<const uint64_t*>(buffer.data());
  const uint64_t* current = start;
  const uint64_t* end = start + internal::BytesToWords(buffer.size());

  while (current < end) {
    auto length = internal::RecordFields::RecordSize::Get<uint16_t>(*current);
    if (length == 0 || length > internal::RecordFields::kMaxRecordSizeBytes ||
        current + length >= end) {
      break;
    }
    current += length;
  }

  return WriteBufferToSocket(buffer.data(),
                             internal::WordsToBytes(current - start), socket);
}

bool Tracee::WriteProviderInfoRecord(const mx::socket& socket) const {
  FTL_DCHECK(bundle_->label.size() <=
             internal::ProviderInfoMetadataRecordFields::kMaxNameLength);

  size_t num_words =
      1u + internal::BytesToWords(internal::Pad(bundle_->label.size()));
  std::vector<uint64_t> record(num_words);
  record[0] =
      internal::ProviderInfoMetadataRecordFields::Type::Make(
          internal::ToUnderlyingType(RecordType::kMetadata)) |
      internal::ProviderInfoMetadataRecordFields::RecordSize::Make(num_words) |
      internal::ProviderInfoMetadataRecordFields::MetadataType::Make(
          internal::ToUnderlyingType(MetadataType::kProviderInfo)) |
      internal::ProviderInfoMetadataRecordFields::Id::Make(bundle_->id) |
      internal::ProviderInfoMetadataRecordFields::NameLength::Make(
          bundle_->label.size());
  memcpy(&record[1], bundle_->label.c_str(), bundle_->label.size());
  return WriteBufferToSocket(reinterpret_cast<uint8_t*>(record.data()),
                             internal::WordsToBytes(num_words), socket);
}

}  // namespace tracing
