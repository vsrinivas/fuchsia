// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace_manager/tracee.h"

#include <fbl/algorithm.h>
#include <lib/async/default.h>
#include <trace-engine/fields.h>
#include <trace-provider/provider.h>

#include "lib/fxl/logging.h"

namespace tracing {

namespace {

// Writes |len| bytes from |buffer| to |socket|. Returns
// TransferStatus::kComplete if the entire buffer has been
// successfully transferred. A return value of
// TransferStatus::kReceiverDead indicates that the peer was closed
// during the transfer.
Tracee::TransferStatus WriteBufferToSocket(const zx::socket& socket,
                                           const uint8_t* buffer, size_t len) {
  size_t offset = 0;
  while (offset < len) {
    zx_status_t status = ZX_OK;
    size_t actual = 0;
    if ((status = socket.write(0u, buffer + offset, len - offset, &actual)) <
        0) {
      if (status == ZX_ERR_SHOULD_WAIT) {
        zx_signals_t pending = 0;
        status = socket.wait_one(ZX_SOCKET_WRITABLE | ZX_SOCKET_PEER_CLOSED,
                                 zx::time::infinite(), &pending);
        if (status < 0) {
          FXL_LOG(ERROR) << "Wait on socket failed: " << status;
          return Tracee::TransferStatus::kCorrupted;
        }

        if (pending & ZX_SOCKET_WRITABLE)
          continue;

        if (pending & ZX_SOCKET_PEER_CLOSED) {
          FXL_LOG(ERROR) << "Peer closed while writing to socket";
          return Tracee::TransferStatus::kReceiverDead;
        }
      }

      return Tracee::TransferStatus::kCorrupted;
    }
    offset += actual;
  }

  return Tracee::TransferStatus::kComplete;
}

fuchsia::tracelink::BufferingMode EngineBufferingModeToTracelinkMode(
    trace_buffering_mode_t mode) {
  switch (mode) {
    case TRACE_BUFFERING_MODE_ONESHOT:
      return fuchsia::tracelink::BufferingMode::ONESHOT;
    case TRACE_BUFFERING_MODE_CIRCULAR:
      return fuchsia::tracelink::BufferingMode::CIRCULAR;
    case TRACE_BUFFERING_MODE_STREAMING:
      return fuchsia::tracelink::BufferingMode::STREAMING;
    default:
      __UNREACHABLE;
  }
}

}  // namespace

Tracee::Tracee(TraceProviderBundle* bundle)
    : bundle_(bundle), wait_(this), weak_ptr_factory_(this) {}

Tracee::~Tracee() {
  if (dispatcher_) {
    wait_.Cancel();
    wait_.set_object(ZX_HANDLE_INVALID);
    dispatcher_ = nullptr;
  }
}

bool Tracee::operator==(TraceProviderBundle* bundle) const {
  return bundle_ == bundle;
}

bool Tracee::Start(fidl::VectorPtr<fidl::StringPtr> categories,
                   size_t buffer_size,
                   fuchsia::tracelink::BufferingMode buffering_mode,
                   fit::closure started_callback,
                   fit::closure stopped_callback) {
  FXL_DCHECK(state_ == State::kReady);
  FXL_DCHECK(!buffer_vmo_);
  FXL_DCHECK(started_callback);
  FXL_DCHECK(stopped_callback);

  zx::vmo buffer_vmo;
  zx_status_t status = zx::vmo::create(buffer_size, 0u, &buffer_vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << *bundle_
                   << ": Failed to create trace buffer: status=" << status;
    return false;
  }

  zx::vmo buffer_vmo_for_provider;
  status = buffer_vmo.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_MAP,
                                &buffer_vmo_for_provider);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << *bundle_
                   << ": Failed to duplicate trace buffer for provider: status="
                   << status;
    return false;
  }

  zx::fifo fifo, fifo_for_provider;
  status = zx::fifo::create(kFifoSizeInPackets, sizeof(trace_provider_packet_t),
                            0u, &fifo, &fifo_for_provider);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << *bundle_
                   << ": Failed to create trace buffer fifo: status=" << status;
    return false;
  }

  bundle_->provider->Start(buffering_mode, std::move(buffer_vmo_for_provider),
                           std::move(fifo_for_provider), std::move(categories));

  buffering_mode_ = buffering_mode;
  buffer_vmo_ = std::move(buffer_vmo);
  buffer_vmo_size_ = buffer_size;
  fifo_ = std::move(fifo);
  started_callback_ = std::move(started_callback);
  stopped_callback_ = std::move(stopped_callback);
  wait_.set_object(fifo_.get());
  wait_.set_trigger(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED);
  dispatcher_ = async_get_default_dispatcher();
  status = wait_.Begin(dispatcher_);
  FXL_CHECK(status == ZX_OK) << "Failed to add handler: status=" << status;
  TransitionToState(State::kStartPending);
  return true;
}

void Tracee::Stop() {
  if (state_ != State::kStarted)
    return;
  bundle_->provider->Stop();
  TransitionToState(State::kStopping);
}

void Tracee::TransitionToState(State new_state) {
  FXL_VLOG(2) << *bundle_ << ": Transitioning from " << state_ << " to "
              << new_state;
  state_ = new_state;
}

void Tracee::OnHandleReady(async_dispatcher_t* dispatcher,
                           async::WaitBase* wait, zx_status_t status,
                           const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    OnHandleError(status);
    return;
  }

  zx_signals_t pending = signal->observed;
  FXL_VLOG(2) << *bundle_ << ": pending=0x" << std::hex << pending;
  FXL_DCHECK(pending & (ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED));
  FXL_DCHECK(state_ == State::kStartPending || state_ == State::kStarted ||
             state_ == State::kStopping);

  if (pending & ZX_FIFO_READABLE) {
    OnFifoReadable(dispatcher, wait);
    // Keep reading packets, one per call, until the peer goes away.
    status = wait->Begin(dispatcher);
    if (status != ZX_OK)
      OnHandleError(status);
    return;
  }

  FXL_DCHECK(pending & ZX_FIFO_PEER_CLOSED);
  wait_.set_object(ZX_HANDLE_INVALID);
  dispatcher_ = nullptr;
  TransitionToState(State::kStopped);
  fit::closure stopped_callback = std::move(stopped_callback_);
  FXL_DCHECK(stopped_callback);
  stopped_callback();
}

void Tracee::OnFifoReadable(async_dispatcher_t* dispatcher,
                            async::WaitBase* wait) {
  trace_provider_packet_t packet;
  auto status2 =
      zx_fifo_read(wait_.object(), sizeof(packet), &packet, 1u, nullptr);
  FXL_DCHECK(status2 == ZX_OK);
  if (packet.reserved != 0) {
    FXL_LOG(ERROR) << *bundle_
                   << ": Received bad packet, non-zero reserved field: "
                   << packet.reserved;
    Stop();
    return;
  }

  switch (packet.request) {
    case TRACE_PROVIDER_STARTED:
      // The provider should only be signalling us when it has finished
      // startup.
      if (packet.data32 != TRACE_PROVIDER_FIFO_PROTOCOL_VERSION) {
        FXL_LOG(ERROR) << *bundle_
                       << ": Received bad packet, unexpected version: "
                       << packet.data32;
        Stop();
        break;
      }
      if (packet.data64 != 0) {
        FXL_LOG(ERROR) << *bundle_
                       << ": Received bad packet, non-zero data64 field: "
                       << packet.data64;
        Stop();
        break;
      }
      if (state_ == State::kStartPending) {
        TransitionToState(State::kStarted);
        fit::closure started_callback = std::move(started_callback_);
        FXL_DCHECK(started_callback);
        started_callback();
      } else {
        FXL_LOG(WARNING) << *bundle_
                         << ": Received TRACE_PROVIDER_STARTED in state "
                         << state_;
      }
      break;
    case TRACE_PROVIDER_BUFFER_OVERFLOW:
      if (state_ == State::kStarted || state_ == State::kStopping) {
        FXL_LOG(WARNING)
            << *bundle_
            << ": Records got dropped, probably due to buffer overflow";
      } else {
        FXL_LOG(WARNING)
            << *bundle_ << ": Received TRACE_PROVIDER_BUFFER_OVERFLOW in state "
            << state_;
      }
      break;
    default:
      FXL_LOG(ERROR) << *bundle_ << ": Received bad packet, unknown request: "
                     << packet.request;
      Stop();
      break;
  }
}

void Tracee::OnHandleError(zx_status_t status) {
  FXL_VLOG(2) << *bundle_ << ": error=" << status;
  FXL_DCHECK(status == ZX_ERR_CANCELED);
  FXL_DCHECK(state_ == State::kStartPending || state_ == State::kStarted ||
             state_ == State::kStopping);
  wait_.set_object(ZX_HANDLE_INVALID);
  dispatcher_ = nullptr;
  TransitionToState(State::kStopped);
}

bool Tracee::VerifyBufferHeader(
    const trace::internal::BufferHeaderReader* header) const {
  if (EngineBufferingModeToTracelinkMode(static_cast<trace_buffering_mode_t>(
          header->buffering_mode())) != buffering_mode_) {
    FXL_LOG(ERROR) << *bundle_ << ": header corrupt, wrong buffering mode: "
                   << header->buffering_mode();
    return false;
  }

  return true;
}

Tracee::TransferStatus Tracee::WriteChunk(const zx::socket& socket,
                                          size_t vmo_offset, size_t size,
                                          const char* name) const {
  FXL_VLOG(2) << *bundle_ << ": Writing chunk for " << name << ": vmo offset 0x"
              << std::hex << vmo_offset << ", size 0x" << std::hex << size;

  // TODO(dje): Loop on smaller buffer.
  // Better yet, be able to pass the entire vmo to the socket (still in
  // three chunks: the writer will need vmo,offset,size parameters).
  std::vector<uint8_t> buffer(size);

  if (buffer_vmo_.read(buffer.data(), vmo_offset, size) != ZX_OK) {
    FXL_LOG(ERROR) << *bundle_ << ": Failed to read data from buffer_vmo: "
                   << "offset=" << vmo_offset << ", size=" << size;
    return TransferStatus::kCorrupted;
  }

  auto status = WriteBufferToSocket(socket, buffer.data(), size);
  if (status != TransferStatus::kComplete) {
    FXL_LOG(ERROR) << *bundle_ << ": Failed to write " << name << " records";
  }
  return status;
}

Tracee::TransferStatus Tracee::TransferRecords(const zx::socket& socket) const {
  FXL_DCHECK(socket);
  FXL_DCHECK(buffer_vmo_);

  auto transfer_status = TransferStatus::kComplete;

  if ((transfer_status = WriteProviderInfoRecord(socket)) !=
      TransferStatus::kComplete) {
    FXL_LOG(ERROR) << *bundle_
                   << ": Failed to write provider info record to trace.";
    return transfer_status;
  }

  // TODO(dje): Need a way to get size of header without getting definition.
  trace::internal::trace_buffer_header header_buffer;
  if (buffer_vmo_.read(&header_buffer, 0, sizeof(header_buffer)) != ZX_OK) {
    FXL_LOG(ERROR) << *bundle_ << ": Failed to read header from buffer_vmo";
    return TransferStatus::kCorrupted;
  }

  fbl::unique_ptr<trace::internal::BufferHeaderReader> header;
  auto error = trace::internal::BufferHeaderReader::Create(
      &header_buffer, buffer_vmo_size_, &header);
  if (error != "") {
    FXL_LOG(ERROR) << *bundle_ << ": header corrupt, " << error.c_str();
    return TransferStatus::kCorrupted;
  }
  if (!VerifyBufferHeader(header.get())) {
    return TransferStatus::kCorrupted;
  }

  if (header->num_records_dropped() > 0) {
    FXL_LOG(WARNING) << *bundle_ << ": " << header->num_records_dropped()
                     << " records were dropped";
    // If we can't write the buffer overflow record, it's not the end of the
    // world.
    if (WriteProviderBufferOverflowEvent(socket) != TransferStatus::kComplete) {
      FXL_LOG(ERROR) << *bundle_
                     << ": Failed to write provider event (buffer overflow)"
                        " record to trace.";
    }
  }

  if (header->durable_data_end() > 0) {
    auto offset = header->get_durable_buffer_offset();
    auto size = header->durable_data_end();
    if ((transfer_status = WriteChunk(socket, offset, size, "durable")) !=
        TransferStatus::kComplete) {
      return transfer_status;
    }
  }

  // There's only two buffers, thus the earlier one is not the current one.
  // It's important to process them in chronological order on the off
  // chance that the earlier buffer provides a stringref or threadref
  // referenced by the later buffer.

  auto write_chunk = [this, &socket, &header](int buffer_number) {
    auto size = header->nondurable_data_end(buffer_number);
    if (size > 0) {
      auto offset = header->GetNondurableBufferOffset(buffer_number);
      auto name =
          buffer_number == 0 ? "nondurable buffer 0" : "nondurable buffer 1";
      return WriteChunk(socket, offset, size, name);
    }
    return TransferStatus::kComplete;
  };

  if (header->wrapped_count() > 0) {
    int buffer_number = get_buffer_number(header->wrapped_count() - 1);
    transfer_status = write_chunk(buffer_number);
    if (transfer_status != TransferStatus::kComplete) {
      return transfer_status;
    }
  }
  int buffer_number = get_buffer_number(header->wrapped_count());
  transfer_status = write_chunk(buffer_number);
  if (transfer_status != TransferStatus::kComplete) {
    return transfer_status;
  }

  // Print some stats to assist things like buffer size calculations.
  if (header->buffering_mode() != TRACE_BUFFERING_MODE_ONESHOT &&
      // Don't print anything if nothing was written.
      header->durable_data_end() > kInitRecordSizeBytes) {
    FXL_LOG(INFO) << *bundle_ << " trace stats";
    FXL_LOG(INFO) << "Wrapped count: " << header->wrapped_count();
    FXL_LOG(INFO) << "Durable buffer: 0x" << std::hex
                  << header->durable_data_end() << ", size 0x" << std::hex
                  << header->durable_buffer_size();
    FXL_LOG(INFO) << "Non-durable buffer: 0x" << std::hex
                  << header->nondurable_data_end(0) << ",0x" << std::hex
                  << header->nondurable_data_end(1) << ", size 0x" << std::hex
                  << header->nondurable_buffer_size();
  }

  return TransferStatus::kComplete;
}

Tracee::TransferStatus Tracee::WriteProviderInfoRecord(
    const zx::socket& socket) const {
  std::string label("");  // TODO(ZX-1875): Provide meaningful labels or remove
                          // labels from the trace wire format altogether.
  size_t num_words = 1u + trace::BytesToWords(trace::Pad(label.size()));
  std::vector<uint64_t> record(num_words);
  record[0] =
      trace::ProviderInfoMetadataRecordFields::Type::Make(
          trace::ToUnderlyingType(trace::RecordType::kMetadata)) |
      trace::ProviderInfoMetadataRecordFields::RecordSize::Make(num_words) |
      trace::ProviderInfoMetadataRecordFields::MetadataType::Make(
          trace::ToUnderlyingType(trace::MetadataType::kProviderInfo)) |
      trace::ProviderInfoMetadataRecordFields::Id::Make(bundle_->id) |
      trace::ProviderInfoMetadataRecordFields::NameLength::Make(label.size());
  memcpy(&record[1], label.c_str(), label.size());
  return WriteBufferToSocket(socket, reinterpret_cast<uint8_t*>(record.data()),
                             trace::WordsToBytes(num_words));
}

Tracee::TransferStatus Tracee::WriteProviderBufferOverflowEvent(
    const zx::socket& socket) const {
  size_t num_words = 1u;
  std::vector<uint64_t> record(num_words);
  record[0] =
      trace::ProviderEventMetadataRecordFields::Type::Make(
          trace::ToUnderlyingType(trace::RecordType::kMetadata)) |
      trace::ProviderEventMetadataRecordFields::RecordSize::Make(num_words) |
      trace::ProviderEventMetadataRecordFields::MetadataType::Make(
          trace::ToUnderlyingType(trace::MetadataType::kProviderEvent)) |
      trace::ProviderEventMetadataRecordFields::Id::Make(bundle_->id) |
      trace::ProviderEventMetadataRecordFields::Event::Make(
          trace::ToUnderlyingType(trace::ProviderEventType::kBufferOverflow));
  return WriteBufferToSocket(socket, reinterpret_cast<uint8_t*>(record.data()),
                             trace::WordsToBytes(num_words));
}

std::ostream& operator<<(std::ostream& out, Tracee::State state) {
  switch (state) {
    case Tracee::State::kReady:
      out << "ready";
      break;
    case Tracee::State::kStartPending:
      out << "start pending";
      break;
    case Tracee::State::kStarted:
      out << "started";
      break;
    case Tracee::State::kStopping:
      out << "stopping";
      break;
    case Tracee::State::kStopped:
      out << "stopped";
      break;
  }

  return out;
}

}  // namespace tracing
