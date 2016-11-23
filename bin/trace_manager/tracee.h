// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_MANAGER_TRACEE_H_
#define APPS_TRACING_SRC_TRACE_MANAGER_TRACEE_H_

#include <functional>

#include <mx/socket.h>
#include <mx/vmo.h>

#include "apps/tracing/src/trace_manager/trace_provider_bundle.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"

namespace tracing {

class Tracee : private mtl::MessageLoopHandler {
 public:
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

  bool Start(size_t buffer_size, fidl::Array<fidl::String> categories, ftl::Closure stop_callback);
  void Stop();
  TransferStatus TransferRecords(const mx::socket& socket) const;

  TraceProviderBundle* bundle() const { return bundle_; }

 private:
  // |mtl::MessageLoopHandler|
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending) override;
  void OnHandleError(mx_handle_t handle, mx_status_t error) override;

  TransferStatus WriteProviderInfoRecord(const mx::socket& socket) const;

  TraceProviderBundle* bundle_;
  mx::vmo buffer_vmo_;
  size_t buffer_vmo_size_ = 0u;
  mx::eventpair fence_;
  ftl::Closure stop_callback_;
  mtl::MessageLoop::HandlerKey fence_handler_key_{};

  FTL_DISALLOW_COPY_AND_ASSIGN(Tracee);
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_MANAGER_TRACEE_H_
