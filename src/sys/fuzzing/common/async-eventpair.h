// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_ASYNC_EVENTPAIR_H_
#define SRC_SYS_FUZZING_COMMON_ASYNC_EVENTPAIR_H_

#include <lib/zx/eventpair.h>
#include <zircon/types.h>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-types.h"

namespace fuzzing {

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

  // Clears and sets signals on to this end of the eventpair.
  void SignalSelf(zx_signals_t to_clear, zx_signals_t to_set) const;

  // Clears and sets signals on to the other end of the eventpair.
  void SignalPeer(zx_signals_t to_clear, zx_signals_t to_set) const;

  // Returns the subset of |signals| currently set on this end of the eventpair.
  zx_signals_t GetSignals(zx_signals_t signals) const;

  // Waits to receive one or more of the requested |signals|.
  ZxPromise<zx_signals_t> WaitFor(zx_signals_t signals);

 private:
  zx::eventpair eventpair_;
  ExecutorPtr executor_;
  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(AsyncEventPair);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_ASYNC_EVENTPAIR_H_
