// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_MANAGER_TRACEE_H_
#define GARNET_BIN_TRACE_MANAGER_TRACEE_H_

#include <async/cpp/wait.h>
#include <zx/socket.h>
#include <zx/vmo.h>

#include <functional>
#include <iosfwd>

#include "garnet/bin/trace_manager/trace_provider_bundle.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace tracing {

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
    // The transfer is incomplete and subsequent
    // transfers should not be executed as the underlying
    // stream has been corrupted.
    kCorrupted,
    // The receiver of the transfer went away.
    kReceiverDead,
  };

  explicit Tracee(TraceProviderBundle* bundle);
  ~Tracee();

  bool operator==(TraceProviderBundle* bundle) const;
  bool Start(size_t buffer_size,
             fidl::Array<fidl::String> categories,
             fxl::Closure started_callback,
             fxl::Closure stopped_callback);
  void Stop();
  TransferStatus TransferRecords(const zx::socket& socket) const;

  TraceProviderBundle* bundle() const { return bundle_; }
  State state() const { return state_; }

 private:
  void TransitionToState(State new_state);
  async_wait_result_t OnHandleReady(async_t* async,
                                    zx_status_t status,
                                    const zx_packet_signal_t* signal);

  TransferStatus WriteProviderInfoRecord(const zx::socket& socket) const;
  TransferStatus WriteProviderBufferOverflowEvent(const zx::socket& socket) const;

  TraceProviderBundle* bundle_;
  State state_ = State::kReady;
  zx::vmo buffer_vmo_;
  size_t buffer_vmo_size_ = 0u;
  zx::eventpair fence_;
  fxl::Closure started_callback_;
  fxl::Closure stopped_callback_;
  async_t* async_ = nullptr;
  async::WaitMethod<Tracee, &Tracee::OnHandleReady> wait_;
  bool buffer_overflow_ = false;

  fxl::WeakPtrFactory<Tracee> weak_ptr_factory_;
  FXL_DISALLOW_COPY_AND_ASSIGN(Tracee);
};

std::ostream& operator<<(std::ostream& out, Tracee::State state);

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_MANAGER_TRACEE_H_
