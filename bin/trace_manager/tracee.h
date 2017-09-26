// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_MANAGER_TRACEE_H_
#define GARNET_BIN_TRACE_MANAGER_TRACEE_H_

#include <functional>
#include <iosfwd>

#include <zx/socket.h>
#include <zx/vmo.h>

#include "garnet/bin/trace_manager/trace_provider_bundle.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/tasks/message_loop_handler.h"

namespace tracing {

class Tracee : private fsl::MessageLoopHandler {
 public:
  using ProviderStartedCallback = std::function<void(bool)>;

  enum class State {
    // All systems go, provider hasn't been started, yet.
    kReady,
    // The provider was asked to start.
    kStartPending,
    // The provider has acknowledged the start request, but is not tracing.
    kStartAcknowledged,
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
             fxl::Closure stop_callback,
             ProviderStartedCallback provider_started_callback);
  void Stop();
  TransferStatus TransferRecords(const zx::socket& socket) const;

  TraceProviderBundle* bundle() const { return bundle_; }
  State state() const { return state_; }

 private:
  void TransitionToState(State new_state);
  void OnProviderStarted(bool success);
  // |fsl::MessageLoopHandler|
  void OnHandleReady(zx_handle_t handle,
                     zx_signals_t pending,
                     uint64_t count) override;
  void OnHandleError(zx_handle_t handle, zx_status_t error) override;

  TransferStatus WriteProviderInfoRecord(const zx::socket& socket) const;

  TraceProviderBundle* bundle_;
  State state_ = State::kReady;
  zx::vmo buffer_vmo_;
  size_t buffer_vmo_size_ = 0u;
  zx::eventpair fence_;
  ProviderStartedCallback start_callback_;
  fxl::Closure stop_callback_;
  fsl::MessageLoop::HandlerKey fence_handler_key_{};

  fxl::WeakPtrFactory<Tracee> weak_ptr_factory_;
  FXL_DISALLOW_COPY_AND_ASSIGN(Tracee);
};

std::ostream& operator<<(std::ostream& out, Tracee::State state);

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_MANAGER_TRACEE_H_
