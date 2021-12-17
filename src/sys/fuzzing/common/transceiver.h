// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TRANSCEIVER_H_
#define SRC_SYS_FUZZING_COMMON_TRANSCEIVER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/zx/socket.h>
#include <stddef.h>

#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/run-once.h"
#include "src/sys/fuzzing/common/sync-wait.h"

namespace fuzzing {

// This class spawns a thread which off-loads reading inputs from and writing inputs to sockets.
// This allows quick responses to FIDL methods which take or return a FidlInput, with the actual
// data transfer following.
class Transceiver final {
 public:
  Transceiver();
  ~Transceiver();

  // Asynchronously reads bytes from |input|'s socket into the |Input| passed to |callback|. Invokes
  // |callback| with |ZX_ERR_BAD_STATE| if |Shutdown| has been called.
  using ReceiveCallback = fit::function<void(zx_status_t, Input)>;
  void Receive(FidlInput input, ReceiveCallback callback) FXL_LOCKS_EXCLUDED(mutex_);

  // Asynchronously writes bytes from |input| to the socket of |out_fidl_input|. Returns
  // |ZX_ERR_BAD_STATE| if |Shutdown| has been called.
  zx_status_t Transmit(Input input, FidlInput* out_fidl_input) FXL_LOCKS_EXCLUDED(mutex_);

  // Stages of stopping: close sources of new tasks, and join the current task.
  void Close() { close_.Run(); }
  void Join() { join_.Run(); }

 private:
  // Opaque struct representing one request to the worker thread.
  struct Request;

  // Add a request to the worker's queue. Returns |ZX_ERR_BAD_STATE| if already shut down.
  zx_status_t Pend(std::unique_ptr<Request> request) FXL_LOCKS_EXCLUDED(mutex_);

  // The worker thread body.
  void Worker() FXL_LOCKS_EXCLUDED(mutex_);

  static void ReceiveImpl(FidlInput&& fidl_input, Transceiver::ReceiveCallback callback);
  static void TransmitImpl(const Input& input, zx::socket sender);

  // Stop-related methods.
  void CloseImpl();
  void JoinImpl();

  std::thread worker_;
  std::mutex mutex_;
  std::deque<std::unique_ptr<Request>> requests_ FXL_GUARDED_BY(mutex_);
  bool stopped_ FXL_GUARDED_BY(mutex_) = false;
  SyncWait sync_;

  RunOnce close_;
  RunOnce join_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(Transceiver);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TRANSCEIVER_H_
