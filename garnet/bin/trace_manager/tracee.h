// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_MANAGER_TRACEE_H_
#define GARNET_BIN_TRACE_MANAGER_TRACEE_H_

#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fidl/cpp/vector.h>
#include <lib/fit/function.h>
#include <lib/zx/fifo.h>
#include <lib/zx/socket.h>
#include <lib/zx/vmo.h>

#include <iosfwd>

#include <trace-reader/reader_internal.h>

#include "garnet/bin/trace_manager/trace_provider_bundle.h"
#include "garnet/bin/trace_manager/util.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace tracing {

namespace provider = ::fuchsia::tracing::provider;

class TraceSession;

class Tracee {
 public:
  enum class State {
    // The provider is ready to be initialized.
    kReady,
    // The provider has been initialized.
    kInitialized,
    // The provider was asked to start.
    kStarting,
    // The provider is started and tracing.
    kStarted,
    // The provider is being stopped right now.
    kStopping,
    // The provider is stopped.
    kStopped,
    // The provider is terminating.
    kTerminating,
    // The provider is terminated.
    kTerminated,
  };

  using StartCallback = fit::closure;
  using StopCallback = fit::function<void(bool write_results)>;
  using TerminateCallback = fit::closure;

  // The size of the initialization record.
  static constexpr size_t kInitRecordSizeBytes = 16;

  explicit Tracee(const TraceSession* session, const TraceProviderBundle* bundle);
  ~Tracee();

  bool operator==(TraceProviderBundle* bundle) const;

  bool Initialize(fidl::VectorPtr<std::string> categories, size_t buffer_size,
                  provider::BufferingMode buffering_mode, StartCallback start_callback,
                  StopCallback stop_callback, TerminateCallback terminate_callback);

  void Terminate();

  void Start(controller::BufferDisposition buffer_disposition,
             const std::vector<std::string>& additional_categories);

  void Stop(bool write_results);

  // Transfer all collected records to |socket|.
  TransferStatus TransferRecords(const zx::socket& socket) const;

  // Save the buffer specified by |wrapped_count|.
  // This is a callback from the TraceSession loop.
  // That's why the result is void and not Tracee::TransferStatus.
  void TransferBuffer(const zx::socket& socket, uint32_t wrapped_count, uint64_t durable_data_end);

  // Helper for |TransferBuffer()|, returns true on success.
  bool DoTransferBuffer(const zx::socket& socket, uint32_t wrapped_count,
                        uint64_t durable_data_end);

  const TraceProviderBundle* bundle() const { return bundle_; }
  State state() const { return state_; }
  bool was_started() const { return was_started_; }
  bool results_written() const { return results_written_; }

 private:
  // The size of the fifo, in packets.
  // TODO(dje): The value will need playing with.
  static constexpr size_t kFifoSizeInPackets = 4u;

  // Given |wrapped_count|, return the corresponding buffer number.
  static int get_buffer_number(uint32_t wrapped_count) { return wrapped_count & 1; }

  // TODO(dje): Until fidl prints names.
  static const char* ModeName(provider::BufferingMode mode);

  void TransitionToState(State new_state);
  void OnHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                     const zx_packet_signal_t* signal);
  void OnFifoReadable(async_dispatcher_t* dispatcher, async::WaitBase* wait);
  void OnHandleError(zx_status_t status);

  bool VerifyBufferHeader(const trace::internal::BufferHeaderReader* header) const;

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
  TransferStatus DoWriteChunk(const zx::socket& socket, size_t vmo_offset, size_t size,
                              const char* name, bool by_size) const;

  TransferStatus WriteChunkByRecords(const zx::socket& socket, uint64_t vmo_offset, uint64_t size,
                                     const char* name) const;
  TransferStatus WriteChunkBySize(const zx::socket& socket, uint64_t vmo_offset, uint64_t size,
                                  const char* name) const;
  TransferStatus WriteChunk(const zx::socket& socket, uint64_t offset, uint64_t last, uint64_t end,
                            uint64_t buffer_size, const char* name) const;

  // Write a ProviderInfo record the first time this is called.
  // For subsequent calls write a ProviderSection record.
  // The ProviderInfo record defines the provider, and subsequent
  // ProviderSection records tell the reader to switch back to that provider.
  TransferStatus WriteProviderIdRecord(const zx::socket& socket) const;

  TransferStatus WriteProviderInfoRecord(const zx::socket& socket) const;
  TransferStatus WriteProviderSectionRecord(const zx::socket& socket) const;
  TransferStatus WriteProviderBufferOverflowEvent(const zx::socket& socket) const;

  void NotifyBufferSaved(uint32_t wrapped_count, uint64_t durable_data_end);

  // Called when a problem is detected warranting shutting the connection down.
  void Abort();

  const TraceSession* const session_;
  const TraceProviderBundle* const bundle_;
  State state_ = State::kReady;

  provider::BufferingMode buffering_mode_;
  zx::vmo buffer_vmo_;
  size_t buffer_vmo_size_ = 0u;
  zx::fifo fifo_;

  StartCallback start_callback_;
  StopCallback stop_callback_;
  TerminateCallback terminate_callback_;

  async_dispatcher_t* dispatcher_ = nullptr;
  async::WaitMethod<Tracee, &Tracee::OnHandleReady> wait_;

  uint32_t last_wrapped_count_ = 0u;
  uint64_t last_durable_data_end_ = 0;
  mutable bool provider_info_record_written_ = false;

  // Set to true when starting. This is used to not write any results,
  // including provider info, if the tracee was never started.
  bool was_started_ = false;

  // The |write_results| flag passed to |Stop()|.
  // We do nothing with this except to pass it back to |stop_callback_|.
  bool write_results_ = false;

  // Set to false when starting and true when results are written.
  // This is used to not save the results twice when terminating.
  mutable bool results_written_ = false;

  fxl::WeakPtrFactory<Tracee> weak_ptr_factory_;

  Tracee(const Tracee&) = delete;
  Tracee(Tracee&&) = delete;
  Tracee& operator=(const Tracee&) = delete;
  Tracee& operator=(Tracee&&) = delete;
};

std::ostream& operator<<(std::ostream& out, Tracee::State state);

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_MANAGER_TRACEE_H_
