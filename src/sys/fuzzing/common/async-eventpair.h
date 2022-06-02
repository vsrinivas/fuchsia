// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_ASYNC_EVENTPAIR_H_
#define SRC_SYS_FUZZING_COMMON_ASYNC_EVENTPAIR_H_

#include <lib/zx/eventpair.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-types.h"

namespace fuzzing {

constexpr zx_signals_t kSyncSignal = ZX_USER_SIGNAL_0;
constexpr zx_signals_t kStartSignal = ZX_USER_SIGNAL_1;
constexpr zx_signals_t kFinishSignal = ZX_USER_SIGNAL_2;
constexpr zx_signals_t kLeakSignal = ZX_USER_SIGNAL_3;

// This enum renames some Zircon user signals to associate them with certain actions performed by
// the engine.
enum Signal : zx_signals_t {
  // Sent by the engine to the targets after it has added a process or module proxy object for them.
  kSync = kSyncSignal,

  // Sent by the engine to the targets at the start of a fuzzing run, and echoed by the targets back
  // to the engine as acknowledgement.
  kStart = kStartSignal,

  // Sent by the engine to the targets at the end of a fuzzing run. Targets will echo with the same
  // or with |kFinishWithLeaks|, depending on whether they suspect a memory leak.
  kFinish = kFinishSignal,

  // Sent by the engine to the targets at the start of a fuzzing run in which leak detection should
  // be enabled. Targets will acknowledge with |kStart|.
  kStartLeakCheck = kStartSignal | kLeakSignal,

  // Sent by the targets to acknowledge receiving |kFinish| when a memory leak is suspected.
  kFinishWithLeaks = kFinishSignal | kLeakSignal,
};

// This class wraps an eventpair to facilitate sending and asynchronously receiving signals with
// additional error-checking.
class AsyncEventPair final {
 public:
  explicit AsyncEventPair(ExecutorPtr executor);
  ~AsyncEventPair() = default;

  const zx::eventpair& eventpair() const { return eventpair_; }
  const ExecutorPtr& executor() const { return executor_; }

  // Creates an event pair and returns one end via |out|. If this object was previously created or
  // linked, it is first reset.
  zx::eventpair Create();

  // Takes one end of an event pair. If this object was previously paired, it is first reset.
  void Pair(zx::eventpair&& eventpair);

  // Returns whether the eventpair is valid and hasn't seen a "peer closed" signal.
  bool IsConnected();

  // Clears and sets user signals on to this end of the eventpair. Non-user signals are ignored.
  // Returns an error if not connected.
  __WARN_UNUSED_RESULT zx_status_t SignalSelf(zx_signals_t to_clear, zx_signals_t to_set);

  // Clears and sets user signals on to the other end of the eventpair. Non-user signals are
  // ignored. Returns an error if not connected.
  __WARN_UNUSED_RESULT zx_status_t SignalPeer(zx_signals_t to_clear, zx_signals_t to_set);

  // Returns the subset of |signals| currently set on this end of the eventpair.
  zx_signals_t GetSignals(zx_signals_t signals);

  // Promises to receive one or more of the requested |signals|. If the object receives a
  // |ZX_EVENTPAIR_PEER_CLOSED| signal, it will return a |ZX_ERR_PEER_CLOSED| error, even if that
  // signal was one of the requested |signals|.
  ZxPromise<zx_signals_t> WaitFor(zx_signals_t signals);

  // Resets the underlying eventpair.
  void Reset();

 private:
  zx::eventpair eventpair_;
  ExecutorPtr executor_;
  fpromise::suspended_task suspended_;
  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(AsyncEventPair);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_ASYNC_EVENTPAIR_H_
