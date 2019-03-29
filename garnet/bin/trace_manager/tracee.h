// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_MANAGER_TRACEE_H_
#define GARNET_BIN_TRACE_MANAGER_TRACEE_H_

#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/zx/fifo.h>
#include <lib/zx/socket.h>
#include <lib/zx/vmo.h>
#include <trace-reader/reader_internal.h>

#include <iosfwd>

#include "garnet/bin/trace_manager/trace_provider_bundle.h"
#include "lib/fidl/cpp/string.h"
#include "lib/fidl/cpp/vector.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace tracing {

class TraceSession;

class Tracee {
 public:
  enum class State {
    // All systems go, provider hasn't been started, yet.
    kReady,
    // The provider was asked to start.
    kStartPending,
    // The provider is started and tracing.
    kStarted,
    // The provider is being stopped right now.
    kStopping,
    // The provider is stopped.
    kStopped
  };

  enum class TransferStatus {
    // The transfer is complete.
    kComplete,
    // An error was detected with the provider, ignore its contribution to
    // trace output.
    kProviderError,
    // Writing of trace data to the receiver failed in an unrecoverable way.
    kWriteError,
    // The receiver of the transfer went away.
    kReceiverDead,
  };

  // The size of the initialization record.
  static constexpr size_t kInitRecordSizeBytes = 16;

  explicit Tracee(const TraceSession* session,
                  const TraceProviderBundle* bundle);
  ~Tracee();

  bool operator==(TraceProviderBundle* bundle) const;
  bool Start(fidl::VectorPtr<std::string> categories, size_t buffer_size,
             fuchsia::tracelink::BufferingMode buffering_mode,
             fit::closure started_callback, fit::closure stopped_callback);
  void Stop();

  // Called once at the end of the trace to transfer all collected records
  // to |socket|.
  TransferStatus TransferRecords(const zx::socket& socket) const;

  // Save the buffer specified by |wrapped_count|.
  // This is a callback from the TraceSession loop.
  // That's why the result is void and not Tracee::TransferStatus.
  void TransferBuffer(const zx::socket& socket, uint32_t wrapped_count,
                      uint64_t durable_data_end);

  // Helper for |TransferBuffer()|, returns true on success.
  bool DoTransferBuffer(const zx::socket& socket, uint32_t wrapped_count,
                        uint64_t durable_data_end);

  const TraceProviderBundle* bundle() const { return bundle_; }
  State state() const { return state_; }

 private:
  // The size of the fifo, in packets.
  // TODO(dje): The value will need playing with.
  static constexpr size_t kFifoSizeInPackets = 4u;

  // Given |wrapped_count|, return the corresponding buffer number.
  static int get_buffer_number(uint32_t wrapped_count) {
    return wrapped_count & 1;
  }

  // TODO(dje): Until fidl prints names.
  static const char* ModeName(fuchsia::tracelink::BufferingMode mode);

  void TransitionToState(State new_state);
  void OnHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                     zx_status_t status, const zx_packet_signal_t* signal);
  void OnFifoReadable(async_dispatcher_t* dispatcher, async::WaitBase* wait);
  void OnHandleError(zx_status_t status);

  bool VerifyBufferHeader(
      const trace::internal::BufferHeaderReader* header) const;

  // Write the records in the buffer at |vmo_offset| to |socket|.
  // |size| is the size in bytes of the chunk to examine, which may be more
  // than was written if |by_size| is false. It must always be a multiple of 8.
  //
  // In oneshot mode we assume the end of written records don't look like
  // records and we can just run through the buffer examining records to
  // compute how many are there. This is problematic (without extra effort) in
  // circular and streaming modes as records are written and rewritten.
  // This function handles both cases. If |by_size| is false then run through
  // the buffer computing the size of each record until we find no more
  // records. If |by_size| is true then |size| is the number of bytes to write.
  TransferStatus DoWriteChunk(const zx::socket& socket, size_t vmo_offset,
                              size_t size, const char* name,
                              bool by_size) const;

  TransferStatus WriteChunkByRecords(const zx::socket& socket,
                                     uint64_t vmo_offset,
                                     uint64_t size,
                                     const char* name) const;
  TransferStatus WriteChunkBySize(const zx::socket& socket,
                                  uint64_t vmo_offset,
                                  uint64_t size,
                                  const char* name) const;
  TransferStatus WriteChunk(const zx::socket& socket,
                            uint64_t offset, uint64_t last,
                            uint64_t end, uint64_t buffer_size,
                            const char* name) const;

  // Write a ProviderInfo record the first time this is called.
  // For subsequent calls write a ProviderSection record.
  // The ProviderInfo record defines the provider, and subsequent
  // ProviderSection records tell the reader to switch back to that provider.
  TransferStatus WriteProviderIdRecord(const zx::socket& socket) const;

  TransferStatus WriteProviderInfoRecord(const zx::socket& socket) const;
  TransferStatus WriteProviderSectionRecord(const zx::socket& socket) const;
  TransferStatus WriteProviderBufferOverflowEvent(
      const zx::socket& socket) const;

  void NotifyBufferSaved(uint32_t wrapped_count, uint64_t durable_data_end);

  const TraceSession* const session_;
  const TraceProviderBundle* const bundle_;
  State state_ = State::kReady;
  fuchsia::tracelink::BufferingMode buffering_mode_;
  zx::vmo buffer_vmo_;
  size_t buffer_vmo_size_ = 0u;
  zx::fifo fifo_;
  fit::closure started_callback_;
  fit::closure stopped_callback_;
  async_dispatcher_t* dispatcher_ = nullptr;
  async::WaitMethod<Tracee, &Tracee::OnHandleReady> wait_;
  uint32_t last_wrapped_count_ = 0u;
  uint64_t last_durable_data_end_ = 0;
  mutable bool provider_info_record_written_ = false;

  fxl::WeakPtrFactory<Tracee> weak_ptr_factory_;
  FXL_DISALLOW_COPY_AND_ASSIGN(Tracee);
};

std::ostream& operator<<(std::ostream& out, Tracee::State state);

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_MANAGER_TRACEE_H_
