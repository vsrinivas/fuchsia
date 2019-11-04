// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_EXCEPTION_BROKER_PROCESS_LIMBO_MANAGER_H_
#define SRC_DEVELOPER_EXCEPTION_BROKER_PROCESS_LIMBO_MANAGER_H_

#include <fuchsia/exception/cpp/fidl.h>

#include <map>

#include "src/lib/fxl/memory/weak_ptr.h"

namespace fuchsia {
namespace exception {

class ProcessLimboHandler;

class ProcessLimboManager {
 public:
  ProcessLimboManager();

  fxl::WeakPtr<ProcessLimboManager> GetWeakPtr();

  void AddToLimbo(ProcessException);

  void AddHandler(fxl::WeakPtr<ProcessLimboHandler> handler);

  // Returns true if there was a change of state.
  bool SetActive(bool active);
  bool active() const { return active_; }

  const std::map<zx_koid_t, ProcessException>& limbo() const { return limbo_; }

 private:
  // TODO(donosoc): This is an extremely naive approach.
  //                There are several policies to make this more robust:
  //                - Put a ceiling on the amount of exceptions to be held.
  //                - Define an eviction policy (FIFO probably).
  //                - Set a timeout for exceptions (configurable).
  //                - Decide on a throttle mechanism (if the same process is crashing continously).
  std::map<zx_koid_t, ProcessException> limbo_;

  // TODO(donosoc): This should be moved into reading a config file at startup.
  //                Exposed for testing purposes.
  bool active_ = false;

  std::vector<fxl::WeakPtr<ProcessLimboHandler>> handlers_;

  fxl::WeakPtrFactory<ProcessLimboManager> weak_factory_;
  friend class ProcessLimboHandler;
};

// Handles *one* process limbo connection. Having one handler per connection lets us do patterns
// like hanging get, which requires to recongnize per-connection state. The limbo manager is the
// common state all connections query.
class ProcessLimboHandler : public ProcessLimbo {
 public:
  explicit ProcessLimboHandler(fxl::WeakPtr<ProcessLimboManager> limbo_manager);
  virtual ~ProcessLimboHandler();

  fxl::WeakPtr<ProcessLimboHandler> GetWeakPtr();

  virtual void ActiveStateChanged(bool state);

  // fuchsia.exception.ProcessLimbo implementation.

  void WatchActive(WatchActiveCallback) override;

  void ListProcessesWaitingOnException(ListProcessesWaitingOnExceptionCallback) override;

  void RetrieveException(zx_koid_t process_koid, RetrieveExceptionCallback) override;

  void ReleaseProcess(zx_koid_t process_koid, ReleaseProcessCallback) override;

 private:
  // WatchActive hanging get.
  bool is_first_watch_active_call_ = true;
  WatchActiveCallback is_active_callback_;

  fxl::WeakPtr<ProcessLimboManager> limbo_manager_;

  fxl::WeakPtrFactory<ProcessLimboHandler> weak_factory_;
};

}  // namespace exception
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_EXCEPTION_BROKER_PROCESS_LIMBO_MANAGER_H_
