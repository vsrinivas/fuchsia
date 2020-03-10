// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace_manager/tests/fake_provider.h"

#include <trace-engine/buffer_internal.h>
#include <trace-engine/fields.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace tracing {
namespace test {

FakeProvider::FakeProvider(zx_koid_t pid, const std::string& name) : pid_(pid), name_(name) {}

std::string FakeProvider::PrettyName() const {
  return fxl::StringPrintf("{%lu:%s}", pid_, name_.c_str());
}

// fidl
void FakeProvider::Initialize(provider::ProviderConfig config) {
  FXL_VLOG(2) << PrettyName() << ": Received Initialize message";
  ++initialize_count_;

  if (state_ != State::kReady) {
    FXL_VLOG(2) << "Can't initialize, state is " << state_;
    return;
  }

  EXPECT_TRUE(config.buffer);
  EXPECT_TRUE(config.fifo);
  if (!config.buffer || !config.fifo) {
    return;
  }

  AdvanceToState(State::kInitialized);

  buffering_mode_ = config.buffering_mode;
  // We need to save |vmo_| and especially |fifo_| - otherwise they'll get
  // closed and trace-manager will interpret that as us going away.
  buffer_vmo_ = std::move(config.buffer);
  fifo_ = std::move(config.fifo);
  categories_ = std::move(config.categories);

  InitializeBuffer();
  // Write the trace initialization record in case Start is called with
  // |BufferDisposition::RETAIN|.
  WriteInitRecord();
}

// fidl
void FakeProvider::Start(provider::StartOptions options) {
  FXL_VLOG(2) << PrettyName() << ": Received Start message";
  ++start_count_;

  if (state_ == State::kInitialized || state_ == State::kStopped) {
    AdvanceToState(State::kStarting);
  } else {
    FXL_VLOG(2) << "Can't start, state is " << state_;
    return;
  }

  if (options.buffer_disposition == provider::BufferDisposition::RETAIN) {
    // Don't reset the buffer pointer.
    FXL_VLOG(2) << "Retaining buffer contents";
  } else {
    // Our fake provider doesn't use the durable buffer, and only one of the
    // rolling buffers.
    FXL_VLOG(2) << "Clearing buffer contents";
    ResetBufferPointers();
    WriteInitRecord();
  }

  WriteBlobRecord();
}

// fidl
void FakeProvider::Stop() {
  FXL_VLOG(2) << PrettyName() << ": Received Stop message";
  ++stop_count_;

  switch (state_) {
    case State::kInitialized:
    case State::kStarting:
    case State::kStarted:
      AdvanceToState(State::kStopping);
      break;
    default:
      FXL_VLOG(2) << "Can't stop, state is " << state_;
      break;
  }
}

// fidl
void FakeProvider::Terminate() {
  FXL_VLOG(2) << PrettyName() << ": Received Terminate message";
  ++terminate_count_;

  switch (state_) {
    case State::kReady:
    case State::kTerminating:
    case State::kTerminated:
      // Nothing to do.
      FXL_VLOG(2) << "Won't advance state, state is " << state_;
      break;
    default:
      AdvanceToState(State::kTerminating);
      break;
  }
}

void FakeProvider::MarkStarted() {
  FXL_DCHECK(state_ == State::kStarting) << state_;
  AdvanceToState(State::kStarted);
}

void FakeProvider::MarkStopped() {
  FXL_DCHECK(state_ == State::kStopping) << state_;
  AdvanceToState(State::kStopped);
}

void FakeProvider::MarkTerminated() {
  FXL_DCHECK(state_ == State::kTerminating) << state_;
  AdvanceToState(State::kTerminated);
}

void FakeProvider::AdvanceToState(State state) {
  FXL_VLOG(2) << PrettyName() << ": Advancing to state " << state;

  switch (state) {
    case State::kReady:
      // We start out in the ready state, tests should never transition us back.
      FXL_NOTREACHED();
      break;
    case State::kInitialized:
    case State::kStarting:
    case State::kStopping:
    case State::kTerminating:
      // Nothing to do.
      break;
    case State::kStarted: {
      trace_provider_packet_t packet{};
      packet.request = TRACE_PROVIDER_STARTED;
      packet.data32 = TRACE_PROVIDER_FIFO_PROTOCOL_VERSION;
      SendFifoPacket(&packet);
      break;
    }
    case State::kStopped: {
      UpdateBufferHeaderAfterStopped();
      trace_provider_packet_t packet{};
      packet.request = TRACE_PROVIDER_STOPPED;
      SendFifoPacket(&packet);
      break;
    }
    case State::kTerminated:
      UpdateBufferHeaderAfterStopped();
      // Tell trace-manager we've finished terminating.
      buffer_vmo_.reset();
      fifo_.reset();
      break;
  }

  state_ = state;
}

bool FakeProvider::SendFifoPacket(const trace_provider_packet_t* packet) {
  zx_status_t status = fifo_.write(sizeof(*packet), packet, 1, nullptr);
  return status == ZX_OK || status == ZX_ERR_PEER_CLOSED;
}

void FakeProvider::InitializeBuffer() {
  ComputeBufferSizes();
  ResetBufferPointers();
  InitBufferHeader();

  // Defensively write zero-length records at the start of each buffer.
  // E.g., We don't emit any records to the durable buffer so ensure
  // TraceManager will see the buffer beginning with a zero-length record
  // which tells it there isn't any.
  if (buffering_mode_ == provider::BufferingMode::ONESHOT) {
    size_t rolling_buffer0_offset = kHeaderSize;
    WriteZeroLengthRecord(rolling_buffer0_offset);
  } else {
    size_t durable_buffer_offset = kHeaderSize;
    WriteZeroLengthRecord(durable_buffer_offset);
    size_t rolling_buffer0_offset = durable_buffer_offset + durable_buffer_size_;
    WriteZeroLengthRecord(rolling_buffer0_offset);
    WriteZeroLengthRecord(rolling_buffer0_offset + rolling_buffer_size_);
  }
}

void FakeProvider::ComputeBufferSizes() {
  zx_status_t status = buffer_vmo_.get_size(&total_buffer_size_);
  FXL_DCHECK(status == ZX_OK) << "status=" << status;

  size_t header_size = kHeaderSize;

  // See trace-engine's |trace_context::ComputeBufferSizes()|.
  switch (buffering_mode_) {
    case provider::BufferingMode::ONESHOT:
      durable_buffer_size_ = 0;
      rolling_buffer_size_ = total_buffer_size_ - header_size;
      break;
    case provider::BufferingMode::CIRCULAR:
    case provider::BufferingMode::STREAMING: {
      size_t avail = total_buffer_size_ - header_size;
      durable_buffer_size_ = kDurableBufferSize;
      uint64_t off_by = (avail - durable_buffer_size_) & 15;
      durable_buffer_size_ += off_by;
      rolling_buffer_size_ = (avail - durable_buffer_size_) / 2;
      // Ensure entire buffer is used.
      FXL_DCHECK(durable_buffer_size_ + 2 * rolling_buffer_size_ == avail);
      break;
    }
    default:
      FXL_NOTREACHED();
      break;
  }
}

void FakeProvider::ResetBufferPointers() {
  FXL_VLOG(2) << PrettyName() << ": Resetting buffer pointers";
  buffer_next_ = 0;
}

void FakeProvider::InitBufferHeader() {
  FXL_VLOG(2) << PrettyName() << ": Initializing buffer header";

  // See trace-engine/context.cpp.
  trace::internal::trace_buffer_header header{};

  header.magic = TRACE_BUFFER_HEADER_MAGIC;
  header.version = TRACE_BUFFER_HEADER_V0;

  switch (buffering_mode_) {
    case provider::BufferingMode::ONESHOT:
      header.buffering_mode = static_cast<uint8_t>(TRACE_BUFFERING_MODE_ONESHOT);
      break;
    case provider::BufferingMode::CIRCULAR:
      header.buffering_mode = static_cast<uint8_t>(TRACE_BUFFERING_MODE_CIRCULAR);
      break;
    case provider::BufferingMode::STREAMING:
      header.buffering_mode = static_cast<uint8_t>(TRACE_BUFFERING_MODE_STREAMING);
      break;
  }

  header.total_size = total_buffer_size_;
  header.durable_buffer_size = durable_buffer_size_;
  header.rolling_buffer_size = rolling_buffer_size_;

  zx_status_t status = buffer_vmo_.write(&header, 0, sizeof(header));
  FXL_DCHECK(status == ZX_OK) << "status=" << status;
}

void FakeProvider::UpdateBufferHeaderAfterStopped() {
  FXL_VLOG(2) << PrettyName() << ": Updating buffer header, buffer pointer=" << buffer_next_;
  size_t offset = offsetof(trace::internal::trace_buffer_header, rolling_data_end[0]);
  zx_status_t status =
      buffer_vmo_.write(reinterpret_cast<uint8_t*>(&buffer_next_), offset, sizeof(uint64_t));
  FXL_DCHECK(status == ZX_OK) << "status=" << status;
}

void FakeProvider::WriteInitRecord() {
  // This record is expected to be the first record.
  // See |trace_context_write_initialization_record()|.
  FXL_VLOG(2) << PrettyName() << ": Writing init record";
  size_t num_words = 2u;
  std::vector<uint64_t> record(num_words);
  record[0] =
      trace::RecordFields::Type::Make(ToUnderlyingType(trace::RecordType::kInitialization)) |
      trace::RecordFields::RecordSize::Make(num_words);
  record[1] = 42;  // #ticks/second
  WriteRecordToBuffer(reinterpret_cast<uint8_t*>(record.data()), trace::WordsToBytes(num_words));
}

void FakeProvider::WriteBlobRecord() {
  FXL_VLOG(2) << PrettyName() << ": Writing blob record";
  size_t num_words = 1u;
  std::vector<uint64_t> record(num_words);
  record[0] =
      trace::BlobRecordFields::Type::Make(trace::ToUnderlyingType(trace::RecordType::kBlob)) |
      trace::BlobRecordFields::RecordSize::Make(num_words) |
      trace::BlobRecordFields::NameStringRef::Make(TRACE_ENCODED_STRING_REF_EMPTY) |
      trace::BlobRecordFields::BlobSize::Make(0) | trace::BlobRecordFields::BlobType::Make(0);
  WriteRecordToBuffer(reinterpret_cast<uint8_t*>(record.data()), trace::WordsToBytes(num_words));
}

void FakeProvider::WriteRecordToBuffer(const uint8_t* data, size_t size) {
  FXL_VLOG(2) << PrettyName() << ": Writing " << size << " bytes at nondurable buffer offset "
              << buffer_next_;
  size_t offset;
  switch (buffering_mode_) {
    case provider::BufferingMode::ONESHOT:
      offset = kHeaderSize + buffer_next_;
      break;
    case provider::BufferingMode::CIRCULAR:
    case provider::BufferingMode::STREAMING:
      offset = kHeaderSize + durable_buffer_size_ + buffer_next_;
      break;
  }
  WriteBytes(data, offset, size);
  buffer_next_ += size;
}

void FakeProvider::WriteZeroLengthRecord(size_t offset) {
  uint64_t zero_length_record = 0;
  WriteBytes(reinterpret_cast<const uint8_t*>(&zero_length_record), offset,
             sizeof(zero_length_record));
}

void FakeProvider::WriteBytes(const uint8_t* data, size_t offset, size_t size) {
  FXL_VLOG(2) << PrettyName() << ": Writing " << size << " bytes at vmo offset " << offset;
  zx_status_t status = buffer_vmo_.write(data, offset, size);
  FXL_DCHECK(status == ZX_OK) << "status=" << status;
}

std::ostream& operator<<(std::ostream& out, FakeProvider::State state) {
  switch (state) {
    case FakeProvider::State::kReady:
      out << "ready";
      break;
    case FakeProvider::State::kInitialized:
      out << "initialized";
      break;
    case FakeProvider::State::kStarting:
      out << "starting";
      break;
    case FakeProvider::State::kStarted:
      out << "started";
      break;
    case FakeProvider::State::kStopping:
      out << "stopping";
      break;
    case FakeProvider::State::kStopped:
      out << "stopped";
      break;
    case FakeProvider::State::kTerminating:
      out << "terminating";
      break;
    case FakeProvider::State::kTerminated:
      out << "terminated";
      break;
  }

  return out;
}

}  // namespace test
}  // namespace tracing
