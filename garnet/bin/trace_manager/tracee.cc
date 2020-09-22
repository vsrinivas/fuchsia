// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace_manager/tracee.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-engine/fields.h>
#include <lib/trace-provider/provider.h>

#include <memory>

#include <fbl/algorithm.h>

#include "garnet/bin/trace_manager/trace_session.h"
#include "garnet/bin/trace_manager/util.h"

namespace tracing {

namespace {

provider::BufferingMode EngineBufferingModeToProviderMode(trace_buffering_mode_t mode) {
  switch (mode) {
    case TRACE_BUFFERING_MODE_ONESHOT:
      return provider::BufferingMode::ONESHOT;
    case TRACE_BUFFERING_MODE_CIRCULAR:
      return provider::BufferingMode::CIRCULAR;
    case TRACE_BUFFERING_MODE_STREAMING:
      return provider::BufferingMode::STREAMING;
    default:
      __UNREACHABLE;
  }
}

uint64_t GetBufferWordsWritten(const uint64_t* buffer, uint64_t size_in_words) {
  const uint64_t* start = buffer;
  const uint64_t* current = start;
  const uint64_t* end = start + size_in_words;

  while (current < end) {
    auto type = trace::RecordFields::Type::Get<trace::RecordType>(*current);
    uint64_t length;
    if (type != trace::RecordType::kLargeRecord) {
      length = trace::RecordFields::RecordSize::Get<size_t>(*current);
    } else {
      length = trace::LargeBlobFields::RecordSize::Get<size_t>(*current);
    }

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
    : session_(session), bundle_(bundle), wait_(this), weak_ptr_factory_(this) {}

Tracee::~Tracee() {
  if (dispatcher_) {
    wait_.Cancel();
    wait_.set_object(ZX_HANDLE_INVALID);
    dispatcher_ = nullptr;
  }
}

bool Tracee::operator==(TraceProviderBundle* bundle) const { return bundle_ == bundle; }

bool Tracee::Initialize(fidl::VectorPtr<std::string> categories, size_t buffer_size,
                        provider::BufferingMode buffering_mode, StartCallback start_callback,
                        StopCallback stop_callback, TerminateCallback terminate_callback,
                        AlertCallback alert_callback) {
  FX_DCHECK(state_ == State::kReady);
  FX_DCHECK(!buffer_vmo_);
  FX_DCHECK(start_callback);
  FX_DCHECK(stop_callback);
  FX_DCHECK(terminate_callback);
  FX_DCHECK(alert_callback);

  zx::vmo buffer_vmo;
  zx_status_t status = zx::vmo::create(buffer_size, 0u, &buffer_vmo);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << *bundle_ << ": Failed to create trace buffer: status=" << status;
    return false;
  }

  zx::vmo buffer_vmo_for_provider;
  status =
      buffer_vmo.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_MAP, &buffer_vmo_for_provider);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << *bundle_
                   << ": Failed to duplicate trace buffer for provider: status=" << status;
    return false;
  }

  zx::fifo fifo, fifo_for_provider;
  status = zx::fifo::create(kFifoSizeInPackets, sizeof(trace_provider_packet_t), 0u, &fifo,
                            &fifo_for_provider);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << *bundle_ << ": Failed to create trace buffer fifo: status=" << status;
    return false;
  }

  provider::ProviderConfig provider_config;
  provider_config.buffering_mode = buffering_mode;
  provider_config.buffer = std::move(buffer_vmo_for_provider);
  provider_config.fifo = std::move(fifo_for_provider);
  if (categories.has_value()) {
    provider_config.categories = std::move(categories.value());
  }
  bundle_->provider->Initialize(std::move(provider_config));

  buffering_mode_ = buffering_mode;
  buffer_vmo_ = std::move(buffer_vmo);
  buffer_vmo_size_ = buffer_size;
  fifo_ = std::move(fifo);

  start_callback_ = std::move(start_callback);
  stop_callback_ = std::move(stop_callback);
  terminate_callback_ = std::move(terminate_callback);
  alert_callback_ = std::move(alert_callback);

  wait_.set_object(fifo_.get());
  wait_.set_trigger(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED);
  dispatcher_ = async_get_default_dispatcher();
  status = wait_.Begin(dispatcher_);
  FX_CHECK(status == ZX_OK) << "Failed to add handler: status=" << status;

  TransitionToState(State::kInitialized);
  return true;
}

void Tracee::Terminate() {
  if (state_ == State::kTerminating || state_ == State::kTerminated) {
    return;
  }
  bundle_->provider->Terminate();
  TransitionToState(State::kTerminating);
}

void Tracee::Start(controller::BufferDisposition buffer_disposition,
                   const std::vector<std::string>& additional_categories) {
  // TraceSession should not call us unless we're ready, either because this
  // is the first time, or subsequent times after tracing has fully stopped
  // from the preceding time.
  FX_DCHECK(state_ == State::kInitialized || state_ == State::kStopped);

  provider::StartOptions start_options;
  switch (buffer_disposition) {
    case controller::BufferDisposition::CLEAR_ALL:
      start_options.buffer_disposition = provider::BufferDisposition::CLEAR_ENTIRE;
      break;
    case controller::BufferDisposition::CLEAR_NONDURABLE:
      start_options.buffer_disposition = provider::BufferDisposition::CLEAR_NONDURABLE;
      break;
    case controller::BufferDisposition::RETAIN:
      start_options.buffer_disposition = provider::BufferDisposition::RETAIN;
      break;
    default:
      FX_NOTREACHED();
      break;
  }
  start_options.additional_categories = additional_categories;
  bundle_->provider->Start(std::move(start_options));

  TransitionToState(State::kStarting);
  was_started_ = true;
  results_written_ = false;
}

void Tracee::Stop(bool write_results) {
  if (state_ != State::kStarting && state_ != State::kStarted) {
    if (state_ == State::kInitialized) {
      // We must have gotten added after tracing started while tracing was
      // being stopped. Mark us as stopped so TraceSession won't try to wait
      // for us to do so.
      TransitionToState(State::kStopped);
    }
    return;
  }
  bundle_->provider->Stop();
  TransitionToState(State::kStopping);
  write_results_ = write_results;
}

void Tracee::TransitionToState(State new_state) {
  FX_VLOGS(2) << *bundle_ << ": Transitioning from " << state_ << " to " << new_state;
  state_ = new_state;
}

void Tracee::OnHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                           zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    OnHandleError(status);
    return;
  }

  zx_signals_t pending = signal->observed;
  FX_VLOGS(2) << *bundle_ << ": pending=0x" << std::hex << pending;
  FX_DCHECK(pending & (ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED));
  FX_DCHECK(state_ != State::kReady && state_ != State::kTerminated);

  if (pending & ZX_FIFO_READABLE) {
    OnFifoReadable(dispatcher, wait);
    // Keep reading packets, one per call, until the peer goes away.
    status = wait->Begin(dispatcher);
    if (status != ZX_OK)
      OnHandleError(status);
    return;
  }

  FX_DCHECK(pending & ZX_FIFO_PEER_CLOSED);
  wait_.set_object(ZX_HANDLE_INVALID);
  dispatcher_ = nullptr;
  TransitionToState(State::kTerminated);
  fit::closure terminate_callback = std::move(terminate_callback_);
  FX_DCHECK(terminate_callback);
  terminate_callback();
}

void Tracee::OnFifoReadable(async_dispatcher_t* dispatcher, async::WaitBase* wait) {
  trace_provider_packet_t packet;
  auto status2 = zx_fifo_read(wait_.object(), sizeof(packet), &packet, 1u, nullptr);
  FX_DCHECK(status2 == ZX_OK);
  if (packet.data16 != 0 && packet.request != TRACE_PROVIDER_ALERT) {
    FX_LOGS(ERROR) << *bundle_ << ": Received bad packet, non-zero data16 field: " << packet.data16;
    Abort();
    return;
  }

  switch (packet.request) {
    case TRACE_PROVIDER_STARTED:
      // The provider should only be signalling us when it has finished
      // startup.
      if (packet.data32 != TRACE_PROVIDER_FIFO_PROTOCOL_VERSION) {
        FX_LOGS(ERROR) << *bundle_
                       << ": Received bad packet, unexpected version: " << packet.data32;
        Abort();
        break;
      }
      if (packet.data64 != 0) {
        FX_LOGS(ERROR) << *bundle_
                       << ": Received bad packet, non-zero data64 field: " << packet.data64;
        Abort();
        break;
      }
      if (state_ == State::kStarting) {
        TransitionToState(State::kStarted);
        start_callback_();
      } else {
        // This could be a problem in the provider or it could just be slow.
        // TODO(dje): Disconnect it and force it to reconnect?
        FX_LOGS(WARNING) << *bundle_ << ": Received TRACE_PROVIDER_STARTED in state " << state_;
      }
      break;
    case TRACE_PROVIDER_SAVE_BUFFER:
      if (buffering_mode_ != provider::BufferingMode::STREAMING) {
        FX_LOGS(WARNING) << *bundle_ << ": Received TRACE_PROVIDER_SAVE_BUFFER in mode "
                         << ModeName(buffering_mode_);
      } else if (state_ == State::kStarted || state_ == State::kStopping ||
                 state_ == State::kTerminating) {
        uint32_t wrapped_count = packet.data32;
        uint64_t durable_data_end = packet.data64;
        // Schedule the write with the main async loop.
        FX_VLOGS(2) << "Buffer save request from " << *bundle_
                    << ", wrapped_count=" << wrapped_count << ", durable_data_end=0x" << std::hex
                    << durable_data_end;
        async::PostTask(dispatcher_, [weak = weak_ptr_factory_.GetWeakPtr(), wrapped_count,
                                      durable_data_end] {
          if (weak) {
            weak->TransferBuffer(weak->session_->destination(), wrapped_count, durable_data_end);
          }
        });
      } else {
        FX_LOGS(WARNING) << *bundle_ << ": Received TRACE_PROVIDER_SAVE_BUFFER in state " << state_;
      }
      break;
    case TRACE_PROVIDER_STOPPED:
      if (packet.data16 != 0 || packet.data32 != 0 || packet.data64 != 0) {
        FX_LOGS(ERROR) << *bundle_ << ": Received bad packet, non-zero data fields";
        Abort();
        break;
      }
      if (state_ == State::kStopping || state_ == State::kTerminating) {
        // If we're terminating leave the transition to kTerminated to
        // noticing the fifo peer closed.
        if (state_ == State::kStopping) {
          TransitionToState(State::kStopped);
        }
        stop_callback_(write_results_);
      } else {
        // This could be a problem in the provider or it could just be slow.
        // TODO(dje): Disconnect it and force it to reconnect?
        FX_LOGS(WARNING) << *bundle_ << ": Received TRACE_PROVIDER_STOPPED in state " << state_;
      }
      break;
    case TRACE_PROVIDER_ALERT: {
      auto p = reinterpret_cast<const char*>(&packet.data16);
      size_t size = sizeof(packet.data16) + sizeof(packet.data32) + sizeof(packet.data64);
      std::string alert_name;
      alert_name.reserve(size);

      for (size_t i = 0; i < size && *p != 0; ++i) {
        alert_name.push_back(*p++);
      }

      alert_callback_(std::move(alert_name));
    } break;
    default:
      FX_LOGS(ERROR) << *bundle_ << ": Received bad packet, unknown request: " << packet.request;
      Abort();
      break;
  }
}

void Tracee::OnHandleError(zx_status_t status) {
  FX_VLOGS(2) << *bundle_ << ": error=" << status;
  FX_DCHECK(status == ZX_ERR_CANCELED);
  FX_DCHECK(state_ != State::kReady && state_ != State::kTerminated);
  wait_.set_object(ZX_HANDLE_INVALID);
  dispatcher_ = nullptr;
  TransitionToState(State::kTerminated);
}

bool Tracee::VerifyBufferHeader(const trace::internal::BufferHeaderReader* header) const {
  if (EngineBufferingModeToProviderMode(
          static_cast<trace_buffering_mode_t>(header->buffering_mode())) != buffering_mode_) {
    FX_LOGS(ERROR) << *bundle_
                   << ": header corrupt, wrong buffering mode: " << header->buffering_mode();
    return false;
  }

  return true;
}

TransferStatus Tracee::DoWriteChunk(const zx::socket& socket, uint64_t vmo_offset, uint64_t size,
                                    const char* name, bool by_size) const {
  FX_VLOGS(2) << *bundle_ << ": Writing chunk for " << name << ": vmo offset 0x" << std::hex
              << vmo_offset << ", size 0x" << std::hex << size
              << (by_size ? ", by-size" : ", by-record");

  // TODO(dje): Loop on smaller buffer.
  // Better yet, be able to pass the entire vmo to the socket (still need to
  // support multiple chunks: the consumer will need vmo,offset,size parameters (fuchsia.mem)).

  uint64_t size_in_words = trace::BytesToWords(size);
  // For paranoia purposes verify size is a multiple of the word size so we
  // don't risk overflowing the buffer later.
  FX_DCHECK(trace::WordsToBytes(size_in_words) == size);
  std::vector<uint64_t> buffer(size_in_words);

  if (buffer_vmo_.read(buffer.data(), vmo_offset, size) != ZX_OK) {
    FX_LOGS(ERROR) << *bundle_ << ": Failed to read data from buffer_vmo: "
                   << "offset=" << vmo_offset << ", size=" << size;
    return TransferStatus::kProviderError;
  }

  uint64_t bytes_written;
  if (!by_size) {
    uint64_t words_written = GetBufferWordsWritten(buffer.data(), size_in_words);
    bytes_written = trace::WordsToBytes(words_written);
    FX_VLOGS(2) << "By-record -> " << bytes_written << " bytes";
  } else {
    bytes_written = size;
  }

  auto status = WriteBufferToSocket(socket, buffer.data(), bytes_written);
  if (status != TransferStatus::kComplete) {
    FX_VLOGS(1) << *bundle_ << ": Failed to write " << name << " records";
  }
  return status;
}

TransferStatus Tracee::WriteChunkByRecords(const zx::socket& socket, uint64_t vmo_offset,
                                           uint64_t size, const char* name) const {
  return DoWriteChunk(socket, vmo_offset, size, name, false);
}

TransferStatus Tracee::WriteChunkBySize(const zx::socket& socket, uint64_t vmo_offset,
                                        uint64_t size, const char* name) const {
  return DoWriteChunk(socket, vmo_offset, size, name, true);
}

TransferStatus Tracee::WriteChunk(const zx::socket& socket, uint64_t offset, uint64_t last,
                                  uint64_t end, uint64_t buffer_size, const char* name) const {
  ZX_DEBUG_ASSERT(last <= buffer_size);
  ZX_DEBUG_ASSERT(end <= buffer_size);
  ZX_DEBUG_ASSERT(end == 0 || last <= end);
  offset += last;
  if (buffering_mode_ == provider::BufferingMode::ONESHOT ||
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
  FX_DCHECK(socket);
  FX_DCHECK(buffer_vmo_);

  auto transfer_status = TransferStatus::kComplete;

  // Regardless of whether we succeed or fail, mark results as being written.
  results_written_ = true;

  if ((transfer_status = WriteProviderIdRecord(socket)) != TransferStatus::kComplete) {
    FX_LOGS(ERROR) << *bundle_ << ": Failed to write provider info record to trace.";
    return transfer_status;
  }

  trace::internal::trace_buffer_header header_buffer;
  if (buffer_vmo_.read(&header_buffer, 0, sizeof(header_buffer)) != ZX_OK) {
    FX_LOGS(ERROR) << *bundle_ << ": Failed to read header from buffer_vmo";
    return TransferStatus::kProviderError;
  }

  std::unique_ptr<trace::internal::BufferHeaderReader> header;
  auto error =
      trace::internal::BufferHeaderReader::Create(&header_buffer, buffer_vmo_size_, &header);
  if (error != "") {
    FX_LOGS(ERROR) << *bundle_ << ": header corrupt, " << error.c_str();
    return TransferStatus::kProviderError;
  }
  if (!VerifyBufferHeader(header.get())) {
    return TransferStatus::kProviderError;
  }

  if (header->num_records_dropped() > 0) {
    FX_LOGS(WARNING) << *bundle_ << ": " << header->num_records_dropped()
                     << " records were dropped";
    // If we can't write the buffer overflow record, it's not the end of the
    // world.
    if (WriteProviderBufferOverflowEvent(socket) != TransferStatus::kComplete) {
      FX_VLOGS(1) << *bundle_
                  << ": Failed to write provider event (buffer overflow) record to trace.";
    }
  }

  if (buffering_mode_ != provider::BufferingMode::ONESHOT) {
    uint64_t offset = header->get_durable_buffer_offset();
    uint64_t last = last_durable_data_end_;
    uint64_t end = header->durable_data_end();
    uint64_t buffer_size = header->durable_buffer_size();
    if ((transfer_status = WriteChunk(socket, offset, last, end, buffer_size, "durable")) !=
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

  auto write_rolling_chunk = [this, &header, &socket](int buffer_number) -> TransferStatus {
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
  // TODO(dje): Revisit this once stats are fully reported back to the client.
  if ((header->buffering_mode() == TRACE_BUFFERING_MODE_ONESHOT &&
       header->rolling_data_end(0) > kInitRecordSizeBytes) ||
      ((header->buffering_mode() != TRACE_BUFFERING_MODE_ONESHOT) &&
       header->durable_data_end() > kInitRecordSizeBytes)) {
    FX_LOGS(INFO) << *bundle_ << " trace stats";
    FX_LOGS(INFO) << "Wrapped count: " << header->wrapped_count();
    FX_LOGS(INFO) << "# records dropped: " << header->num_records_dropped();
    FX_LOGS(INFO) << "Durable buffer: 0x" << std::hex << header->durable_data_end() << ", size 0x"
                  << std::hex << header->durable_buffer_size();
    FX_LOGS(INFO) << "Non-durable buffer: 0x" << std::hex << header->rolling_data_end(0) << ",0x"
                  << std::hex << header->rolling_data_end(1) << ", size 0x" << std::hex
                  << header->rolling_buffer_size();
  }

  return TransferStatus::kComplete;
}

void Tracee::TransferBuffer(const zx::socket& socket, uint32_t wrapped_count,
                            uint64_t durable_data_end) {
  FX_DCHECK(buffering_mode_ == provider::BufferingMode::STREAMING);
  FX_DCHECK(socket);
  FX_DCHECK(buffer_vmo_);

  if (!DoTransferBuffer(socket, wrapped_count, durable_data_end)) {
    Abort();
    return;
  }

  // If a consumer isn't connected we still want to mark the buffer as having
  // been saved in order to keep the trace engine running.
  last_wrapped_count_ = wrapped_count;
  last_durable_data_end_ = durable_data_end;
  NotifyBufferSaved(wrapped_count, durable_data_end);
}

bool Tracee::DoTransferBuffer(const zx::socket& socket, uint32_t wrapped_count,
                              uint64_t durable_data_end) {
  if (wrapped_count == 0 && last_wrapped_count_ == 0) {
    // ok
  } else if (wrapped_count != last_wrapped_count_ + 1) {
    FX_LOGS(ERROR) << *bundle_ << ": unexpected wrapped_count from provider: " << wrapped_count;
    return false;
  } else if (durable_data_end < last_durable_data_end_ || (durable_data_end & 7) != 0) {
    FX_LOGS(ERROR) << *bundle_
                   << ": unexpected durable_data_end from provider: " << durable_data_end;
    return false;
  }

  auto transfer_status = TransferStatus::kComplete;
  int buffer_number = get_buffer_number(wrapped_count);

  if ((transfer_status = WriteProviderIdRecord(socket)) != TransferStatus::kComplete) {
    FX_LOGS(ERROR) << *bundle_ << ": Failed to write provider section record to trace.";
    return false;
  }

  trace::internal::trace_buffer_header header_buffer;
  if (buffer_vmo_.read(&header_buffer, 0, sizeof(header_buffer)) != ZX_OK) {
    FX_LOGS(ERROR) << *bundle_ << ": Failed to read header from buffer_vmo";
    return false;
  }

  std::unique_ptr<trace::internal::BufferHeaderReader> header;
  auto error =
      trace::internal::BufferHeaderReader::Create(&header_buffer, buffer_vmo_size_, &header);
  if (error != "") {
    FX_LOGS(ERROR) << *bundle_ << ": header corrupt, " << error.c_str();
    return false;
  }
  if (!VerifyBufferHeader(header.get())) {
    return false;
  }

  // Don't use |header.durable_data_end| here, we want the value at the time
  // the message was sent.
  if (durable_data_end < kInitRecordSizeBytes || durable_data_end > header->durable_buffer_size() ||
      (durable_data_end & 7) != 0 || durable_data_end < last_durable_data_end_) {
    FX_LOGS(ERROR) << *bundle_ << ": bad durable_data_end: " << durable_data_end;
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
    if ((transfer_status = WriteChunkBySize(socket, durable_buffer_offset + last_durable_data_end_,
                                            size, "durable")) != TransferStatus::kComplete) {
      return false;
    }
  }

  uint64_t buffer_offset = header->GetRollingBufferOffset(buffer_number);
  auto name = buffer_number == 0 ? "rolling buffer 0" : "rolling buffer 1";
  if ((transfer_status = WriteChunkBySize(socket, buffer_offset, rolling_data_end, name)) !=
      TransferStatus::kComplete) {
    return false;
  }

  return true;
}

void Tracee::NotifyBufferSaved(uint32_t wrapped_count, uint64_t durable_data_end) {
  FX_VLOGS(2) << "Buffer saved for " << *bundle_ << ", wrapped_count=" << wrapped_count
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
    Abort();
  } else {
    FX_DCHECK(status == ZX_OK || status == ZX_ERR_PEER_CLOSED);
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
  FX_VLOGS(5) << *bundle_ << ": writing provider info record";
  std::string label("");  // TODO(fxbug.dev/31743): Provide meaningful labels or remove
                          // labels from the trace wire format altogether.
  size_t num_words = 1u + trace::BytesToWords(trace::Pad(label.size()));
  std::vector<uint64_t> record(num_words);
  record[0] = trace::ProviderInfoMetadataRecordFields::Type::Make(
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

TransferStatus Tracee::WriteProviderSectionRecord(const zx::socket& socket) const {
  FX_VLOGS(2) << *bundle_ << ": writing provider section record";
  size_t num_words = 1u;
  std::vector<uint64_t> record(num_words);
  record[0] = trace::ProviderSectionMetadataRecordFields::Type::Make(
                  trace::ToUnderlyingType(trace::RecordType::kMetadata)) |
              trace::ProviderSectionMetadataRecordFields::RecordSize::Make(num_words) |
              trace::ProviderSectionMetadataRecordFields::MetadataType::Make(
                  trace::ToUnderlyingType(trace::MetadataType::kProviderSection)) |
              trace::ProviderSectionMetadataRecordFields::Id::Make(bundle_->id);
  return WriteBufferToSocket(socket, reinterpret_cast<uint8_t*>(record.data()),
                             trace::WordsToBytes(num_words));
}

TransferStatus Tracee::WriteProviderBufferOverflowEvent(const zx::socket& socket) const {
  size_t num_words = 1u;
  std::vector<uint64_t> record(num_words);
  record[0] = trace::ProviderEventMetadataRecordFields::Type::Make(
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

void Tracee::Abort() {
  FX_LOGS(ERROR) << *bundle_ << ": Aborting connection";
  Terminate();
}

const char* Tracee::ModeName(provider::BufferingMode mode) {
  switch (mode) {
    case provider::BufferingMode::ONESHOT:
      return "oneshot";
    case provider::BufferingMode::CIRCULAR:
      return "circular";
    case provider::BufferingMode::STREAMING:
      return "streaming";
  }
}

std::ostream& operator<<(std::ostream& out, Tracee::State state) {
  switch (state) {
    case Tracee::State::kReady:
      out << "ready";
      break;
    case Tracee::State::kInitialized:
      out << "initialized";
      break;
    case Tracee::State::kStarting:
      out << "starting";
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
    case Tracee::State::kTerminating:
      out << "terminating";
      break;
    case Tracee::State::kTerminated:
      out << "terminated";
      break;
  }

  return out;
}

}  // namespace tracing
