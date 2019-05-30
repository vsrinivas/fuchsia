// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace_manager/tracee.h"

#include <fbl/algorithm.h>
#include <lib/async/default.h>
#include <trace-engine/fields.h>
#include <trace-provider/provider.h>

#include "garnet/bin/trace_manager/trace_session.h"
#include "garnet/bin/trace_manager/util.h"
#include "src/lib/fxl/logging.h"

namespace tracing {

namespace {

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

uint64_t GetBufferWordsWritten(const uint64_t* buffer, uint64_t size_in_words) {
  const uint64_t* start = buffer;
  const uint64_t* current = start;
  const uint64_t* end = start + size_in_words;

  while (current < end) {
    auto length = trace::RecordFields::RecordSize::Get<uint16_t>(*current);
    if (length == 0 || length > trace::RecordFields::kMaxRecordSizeBytes ||
        current + length >= end) {
      break;
    }
    current += length;
  }

  return current - start;
}

}  // namespace

Tracee::Tracee(const TraceSession* session, const TraceProviderBundle* bundle)
    : session_(session),
      bundle_(bundle),
      wait_(this),
      weak_ptr_factory_(this) {}

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

bool Tracee::Start(fidl::VectorPtr<std::string> categories, size_t buffer_size,
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
  if (packet.data16 != 0) {
    FXL_LOG(ERROR) << *bundle_
                   << ": Received bad packet, non-zero data16 field: "
                   << packet.data16;
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
    case TRACE_PROVIDER_SAVE_BUFFER:
      if (buffering_mode_ != fuchsia::tracelink::BufferingMode::STREAMING) {
        FXL_LOG(WARNING) << *bundle_
                         << ": Received TRACE_PROVIDER_SAVE_BUFFER in mode "
                         << ModeName(buffering_mode_);
      } else if (state_ == State::kStarted || state_ == State::kStopping) {
        uint32_t wrapped_count = packet.data32;
        uint64_t durable_data_end = packet.data64;
        // Schedule the write with the main async loop.
        FXL_VLOG(2) << "Buffer save request from " << *bundle_
                    << ", wrapped_count=" << wrapped_count
                    << ", durable_data_end=0x" << std::hex << durable_data_end;
        async::PostTask(dispatcher_, [weak = weak_ptr_factory_.GetWeakPtr(),
                                      wrapped_count, durable_data_end] {
          if (weak) {
            weak->TransferBuffer(weak->session_->destination(), wrapped_count,
                                 durable_data_end);
          }
        });
      } else {
        FXL_LOG(WARNING) << *bundle_
                         << ": Received TRACE_PROVIDER_SAVE_BUFFER in state "
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

TransferStatus Tracee::DoWriteChunk(const zx::socket& socket,
                                    uint64_t vmo_offset, uint64_t size,
                                    const char* name, bool by_size) const {
  FXL_VLOG(2) << *bundle_ << ": Writing chunk for " << name << ": vmo offset 0x"
              << std::hex << vmo_offset << ", size 0x" << std::hex << size
              << (by_size ? ", by-size" : ", by-record");

  // TODO(dje): Loop on smaller buffer.
  // Better yet, be able to pass the entire vmo to the socket (still need to
  // support multiple chunks: the writer will need vmo,offset,size parameters).

  uint64_t size_in_words = trace::BytesToWords(size);
  // For paranoia purposes verify size is a multiple of the word size so we
  // don't risk overflowing the buffer later.
  FXL_DCHECK(trace::WordsToBytes(size_in_words) == size);
  std::vector<uint64_t> buffer(size_in_words);

  if (buffer_vmo_.read(buffer.data(), vmo_offset, size) != ZX_OK) {
    FXL_LOG(ERROR) << *bundle_ << ": Failed to read data from buffer_vmo: "
                   << "offset=" << vmo_offset << ", size=" << size;
    return TransferStatus::kProviderError;
  }

  uint64_t bytes_written;
  if (!by_size) {
    uint64_t words_written =
        GetBufferWordsWritten(buffer.data(), size_in_words);
    bytes_written = trace::WordsToBytes(words_written);
  } else {
    bytes_written = size;
  }

  auto status = WriteBufferToSocket(socket, buffer.data(), bytes_written);
  if (status != TransferStatus::kComplete) {
    FXL_LOG(ERROR) << *bundle_ << ": Failed to write " << name << " records";
  }
  return status;
}

TransferStatus Tracee::WriteChunkByRecords(const zx::socket& socket,
                                           uint64_t vmo_offset, uint64_t size,
                                           const char* name) const {
  return DoWriteChunk(socket, vmo_offset, size, name, false);
}

TransferStatus Tracee::WriteChunkBySize(const zx::socket& socket,
                                        uint64_t vmo_offset, uint64_t size,
                                        const char* name) const {
  return DoWriteChunk(socket, vmo_offset, size, name, true);
}

TransferStatus Tracee::WriteChunk(const zx::socket& socket, uint64_t offset,
                                  uint64_t last, uint64_t end,
                                  uint64_t buffer_size,
                                  const char* name) const {
  ZX_DEBUG_ASSERT(last <= buffer_size);
  ZX_DEBUG_ASSERT(end <= buffer_size);
  ZX_DEBUG_ASSERT(end == 0 || last <= end);
  offset += last;
  if (buffering_mode_ == fuchsia::tracelink::BufferingMode::ONESHOT ||
      // If end is zero then the header wasn't updated when tracing stopped.
      end == 0) {
    uint64_t size = buffer_size - last;
    return WriteChunkByRecords(socket, offset, size, name);
  } else {
    uint64_t size = end - last;
    return WriteChunkBySize(socket, offset, size, name);
  }
}

TransferStatus Tracee::TransferRecords(const zx::socket& socket) const {
  FXL_DCHECK(socket);
  FXL_DCHECK(buffer_vmo_);

  auto transfer_status = TransferStatus::kComplete;

  if ((transfer_status = WriteProviderIdRecord(socket)) !=
      TransferStatus::kComplete) {
    FXL_LOG(ERROR) << *bundle_
                   << ": Failed to write provider info record to trace.";
    return transfer_status;
  }

  trace::internal::trace_buffer_header header_buffer;
  if (buffer_vmo_.read(&header_buffer, 0, sizeof(header_buffer)) != ZX_OK) {
    FXL_LOG(ERROR) << *bundle_ << ": Failed to read header from buffer_vmo";
    return TransferStatus::kProviderError;
  }

  fbl::unique_ptr<trace::internal::BufferHeaderReader> header;
  auto error = trace::internal::BufferHeaderReader::Create(
      &header_buffer, buffer_vmo_size_, &header);
  if (error != "") {
    FXL_LOG(ERROR) << *bundle_ << ": header corrupt, " << error.c_str();
    return TransferStatus::kProviderError;
  }
  if (!VerifyBufferHeader(header.get())) {
    return TransferStatus::kProviderError;
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

  if (buffering_mode_ != fuchsia::tracelink::BufferingMode::ONESHOT) {
    uint64_t offset = header->get_durable_buffer_offset();
    uint64_t last = last_durable_data_end_;
    uint64_t end = header->durable_data_end();
    uint64_t buffer_size = header->durable_buffer_size();
    if ((transfer_status =
             WriteChunk(socket, offset, last, end, buffer_size, "durable")) !=
        TransferStatus::kComplete) {
      return transfer_status;
    }
  }

  // There's only two buffers, thus the earlier one is not the current one.
  // It's important to process them in chronological order on the off
  // chance that the earlier buffer provides a stringref or threadref
  // referenced by the later buffer.
  //
  // We want to handle the case of still capturing whatever records we can if
  // the process crashes, in which case the header won't be up to date. In
  // oneshot mode we're covered: We run through the records and see what's
  // there. In circular and streaming modes after a buffer gets reused we can't
  // do that. But if the process crashes it may be the last trace records that
  // are important: we don't want to lose them. As a compromise, if the header
  // is marked as valid use it. Otherwise run through the buffer to count the
  // records we see.

  auto write_rolling_chunk = [this, &header,
                              &socket](int buffer_number) -> TransferStatus {
    uint64_t offset = header->GetRollingBufferOffset(buffer_number);
    uint64_t last = 0;
    uint64_t end = header->rolling_data_end(buffer_number);
    uint64_t buffer_size = header->rolling_buffer_size();
    auto name = buffer_number == 0 ? "rolling buffer 0" : "rolling buffer 1";
    return WriteChunk(socket, offset, last, end, buffer_size, name);
  };

  if (header->wrapped_count() > 0) {
    int buffer_number = get_buffer_number(header->wrapped_count() - 1);
    transfer_status = write_rolling_chunk(buffer_number);
    if (transfer_status != TransferStatus::kComplete) {
      return transfer_status;
    }
  }
  int buffer_number = get_buffer_number(header->wrapped_count());
  transfer_status = write_rolling_chunk(buffer_number);
  if (transfer_status != TransferStatus::kComplete) {
    return transfer_status;
  }

  // Print some stats to assist things like buffer size calculations.
  // Don't print anything if nothing was written.
  if ((header->buffering_mode() == TRACE_BUFFERING_MODE_ONESHOT &&
       header->rolling_data_end(0) > kInitRecordSizeBytes) ||
      ((header->buffering_mode() != TRACE_BUFFERING_MODE_ONESHOT) &&
       header->durable_data_end() > kInitRecordSizeBytes)) {
    FXL_LOG(INFO) << *bundle_ << " trace stats";
    FXL_LOG(INFO) << "Wrapped count: " << header->wrapped_count();
    FXL_LOG(INFO) << "# records dropped: " << header->num_records_dropped();
    FXL_LOG(INFO) << "Durable buffer: 0x" << std::hex
                  << header->durable_data_end() << ", size 0x" << std::hex
                  << header->durable_buffer_size();
    FXL_LOG(INFO) << "Non-durable buffer: 0x" << std::hex
                  << header->rolling_data_end(0) << ",0x" << std::hex
                  << header->rolling_data_end(1) << ", size 0x" << std::hex
                  << header->rolling_buffer_size();
  }

  return TransferStatus::kComplete;
}

void Tracee::TransferBuffer(const zx::socket& socket, uint32_t wrapped_count,
                            uint64_t durable_data_end) {
  FXL_DCHECK(buffering_mode_ == fuchsia::tracelink::BufferingMode::STREAMING);
  FXL_DCHECK(socket);
  FXL_DCHECK(buffer_vmo_);

  if (!DoTransferBuffer(socket, wrapped_count, durable_data_end)) {
    Stop();
  }

  last_wrapped_count_ = wrapped_count;
  last_durable_data_end_ = durable_data_end;
  NotifyBufferSaved(wrapped_count, durable_data_end);
}

bool Tracee::DoTransferBuffer(const zx::socket& socket, uint32_t wrapped_count,
                              uint64_t durable_data_end) {
  if (wrapped_count == 0 && last_wrapped_count_ == 0) {
    // ok
  } else if (wrapped_count != last_wrapped_count_ + 1) {
    FXL_LOG(ERROR) << *bundle_ << ": unexpected wrapped_count from provider: "
                   << wrapped_count;
    return false;
  } else if (durable_data_end < last_durable_data_end_ ||
             (durable_data_end & 7) != 0) {
    FXL_LOG(ERROR) << *bundle_
                   << ": unexpected durable_data_end from provider: "
                   << durable_data_end;
    return false;
  }

  auto transfer_status = TransferStatus::kComplete;
  int buffer_number = get_buffer_number(wrapped_count);

  if ((transfer_status = WriteProviderIdRecord(socket)) !=
      TransferStatus::kComplete) {
    FXL_LOG(ERROR) << *bundle_
                   << ": Failed to write provider section record to trace.";
    return false;
  }

  trace::internal::trace_buffer_header header_buffer;
  if (buffer_vmo_.read(&header_buffer, 0, sizeof(header_buffer)) != ZX_OK) {
    FXL_LOG(ERROR) << *bundle_ << ": Failed to read header from buffer_vmo";
    return false;
  }

  fbl::unique_ptr<trace::internal::BufferHeaderReader> header;
  auto error = trace::internal::BufferHeaderReader::Create(
      &header_buffer, buffer_vmo_size_, &header);
  if (error != "") {
    FXL_LOG(ERROR) << *bundle_ << ": header corrupt, " << error.c_str();
    return false;
  }
  if (!VerifyBufferHeader(header.get())) {
    return false;
  }

  // Don't use |header.durable_data_end| here, we want the value at the time
  // the message was sent.
  if (durable_data_end < kInitRecordSizeBytes ||
      durable_data_end > header->durable_buffer_size() ||
      (durable_data_end & 7) != 0 ||
      durable_data_end < last_durable_data_end_) {
    FXL_LOG(ERROR) << *bundle_
                   << ": bad durable_data_end: " << durable_data_end;
    return false;
  }

  // However we can use rolling_data_end from the header.
  // This buffer is no longer being written to until we save it.
  // [And if it does get written to it'll potentially result in corrupt
  // data, but that's not our problem; as long as we can't crash, which is
  // always the rule here.]
  uint64_t rolling_data_end = header->rolling_data_end(buffer_number);

  // Only transfer what's new in the durable buffer since the last time.
  uint64_t durable_buffer_offset = header->get_durable_buffer_offset();
  if (durable_data_end > last_durable_data_end_) {
    uint64_t size = durable_data_end - last_durable_data_end_;
    if ((transfer_status = WriteChunkBySize(
             socket, durable_buffer_offset + last_durable_data_end_, size,
             "durable")) != TransferStatus::kComplete) {
      return false;
    }
  }

  uint64_t buffer_offset = header->GetRollingBufferOffset(buffer_number);
  auto name = buffer_number == 0 ? "rolling buffer 0" : "rolling buffer 1";
  if ((transfer_status =
           WriteChunkBySize(socket, buffer_offset, rolling_data_end, name)) !=
      TransferStatus::kComplete) {
    return false;
  }

  return true;
}

void Tracee::NotifyBufferSaved(uint32_t wrapped_count,
                               uint64_t durable_data_end) {
  FXL_VLOG(2) << "Buffer saved for " << *bundle_
              << ", wrapped_count=" << wrapped_count
              << ", durable_data_end=" << durable_data_end;
  trace_provider_packet_t packet{};
  packet.request = TRACE_PROVIDER_BUFFER_SAVED;
  packet.data32 = wrapped_count;
  packet.data64 = durable_data_end;
  auto status = fifo_.write(sizeof(packet), &packet, 1, nullptr);
  if (status == ZX_ERR_SHOULD_WAIT) {
    // The FIFO should never fill. If it does then the provider is sending us
    // buffer full notifications but not reading our replies. Terminate the
    // connection.
    Stop();
  } else {
    FXL_DCHECK(status == ZX_OK || status == ZX_ERR_PEER_CLOSED);
  }
}

TransferStatus Tracee::WriteProviderIdRecord(const zx::socket& socket) const {
  if (provider_info_record_written_) {
    return WriteProviderSectionRecord(socket);
  } else {
    auto status = WriteProviderInfoRecord(socket);
    provider_info_record_written_ = true;
    return status;
  }
}

TransferStatus Tracee::WriteProviderInfoRecord(const zx::socket& socket) const {
  FXL_VLOG(2) << *bundle_ << ": writing provider info record";
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

TransferStatus Tracee::WriteProviderSectionRecord(
    const zx::socket& socket) const {
  FXL_VLOG(2) << *bundle_ << ": writing provider section record";
  size_t num_words = 1u;
  std::vector<uint64_t> record(num_words);
  record[0] =
      trace::ProviderSectionMetadataRecordFields::Type::Make(
          trace::ToUnderlyingType(trace::RecordType::kMetadata)) |
      trace::ProviderSectionMetadataRecordFields::RecordSize::Make(num_words) |
      trace::ProviderSectionMetadataRecordFields::MetadataType::Make(
          trace::ToUnderlyingType(trace::MetadataType::kProviderSection)) |
      trace::ProviderSectionMetadataRecordFields::Id::Make(bundle_->id);
  return WriteBufferToSocket(socket, reinterpret_cast<uint8_t*>(record.data()),
                             trace::WordsToBytes(num_words));
}

TransferStatus Tracee::WriteProviderBufferOverflowEvent(
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

const char* Tracee::ModeName(fuchsia::tracelink::BufferingMode mode) {
  switch (mode) {
    case fuchsia::tracelink::BufferingMode::ONESHOT:
      return "oneshot";
    case fuchsia::tracelink::BufferingMode::CIRCULAR:
      return "circular";
    case fuchsia::tracelink::BufferingMode::STREAMING:
      return "streaming";
  }
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
