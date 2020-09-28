// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_PROCESS_LIMBO_MANAGER_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_PROCESS_LIMBO_MANAGER_H_

#include <fuchsia/exception/cpp/fidl.h>

#include <map>
#include <set>

#include "src/lib/fxl/memory/weak_ptr.h"

namespace forensics {
namespace exceptions {

class ProcessLimboHandler;

class ProcessLimboManager {
 public:
  static constexpr size_t kMaxFilters = 32;

  ProcessLimboManager();

  fxl::WeakPtr<ProcessLimboManager> GetWeakPtr();

  void AddToLimbo(fuchsia::exception::ProcessException);

  // Notify all handlers that limbo changed.
  void NotifyLimboChanged();

  void AddHandler(fxl::WeakPtr<ProcessLimboHandler> handler);

  // Returns true if there was a change of state.
  bool SetActive(bool active);
  bool active() const { return active_; }

  const std::map<zx_koid_t, fuchsia::exception::ProcessException>& limbo() const { return limbo_; }

  void set_filters(std::set<std::string> filters) { filters_ = std::move(filters); }
  const std::set<std::string>& filters() const { return filters_; }

  // Testing utilities.
  void AppendFiltersForTesting(const std::vector<std::string>&);

  void set_obtain_process_name_fn(fit::function<std::string(zx_handle_t)> fn) {
    obtain_process_name_fn_ = std::move(fn);
  }

 private:
  // Returns the list of process metadata for processes waiting on exceptions
  // Corresponds to the return value of |WatchProcessesWaitingOnException|.
  std::vector<fuchsia::exception::ProcessExceptionMetadata> ListProcessesInLimbo();

  // TODO(fxbug.dev/45962): This is an extremely naive approach. There are several policies to make this more
  //              robust:
  //                - Put a ceiling on the amount of exceptions to be held.
  //                - Define an eviction policy (FIFO probably).
  //                - Set a timeout for exceptions (configurable).
  //                - Decide on a throttle mechanism (if the same process is crashing continously).
  std::map<zx_koid_t, fuchsia::exception::ProcessException> limbo_;

  bool active_ = false;

  std::vector<fxl::WeakPtr<ProcessLimboHandler>> handlers_;

  std::set<std::string> filters_;

  // Testing won't have valid handles (mocking them is very involved), so we use this function to
  // inject the way the manager will get the process name, permitting to test the filtering.
  fit::function<std::string(zx_handle_t)> obtain_process_name_fn_;

  fxl::WeakPtrFactory<ProcessLimboManager> weak_factory_;
  friend class ProcessLimboHandler;
};

// Handles *one* process limbo connection. Having one handler per connection lets us do patterns
// like hanging get, which requires to recongnize per-connection state. The limbo manager is the
// common state all connections query.
class ProcessLimboHandler : public fuchsia::exception::ProcessLimbo {
 public:
  explicit ProcessLimboHandler(fxl::WeakPtr<ProcessLimboManager> limbo_manager);

  fxl::WeakPtr<ProcessLimboHandler> GetWeakPtr();

  void ActiveStateChanged(bool state);

  // Called when a process goes in or out of limbo (ProcessLimboManager::AddToLimbo).
  void LimboChanged(std::vector<fuchsia::exception::ProcessExceptionMetadata> processes);

  // fuchsia.exception.ProcessLimbo implementation.
  void SetActive(bool active, SetActiveCallback) override;

  void WatchActive(WatchActiveCallback) override;

  void WatchProcessesWaitingOnException(WatchProcessesWaitingOnExceptionCallback) override;

  void RetrieveException(zx_koid_t process_koid, RetrieveExceptionCallback) override;

  void ReleaseProcess(zx_koid_t process_koid, ReleaseProcessCallback) override;

  void GetFilters(GetFiltersCallback) override;

  void AppendFilters(std::vector<std::string> filters, AppendFiltersCallback) override;

  void RemoveFilters(std::vector<std::string> filters, RemoveFiltersCallback) override;

 private:
  // WatchActive hanging get.
  bool watch_active_dirty_bit_ = true;
  WatchActiveCallback is_active_callback_;

  bool watch_limbo_dirty_bit_ = true;
  WatchProcessesWaitingOnExceptionCallback watch_limbo_callback_;

  fxl::WeakPtr<ProcessLimboManager> limbo_manager_;

  fxl::WeakPtrFactory<ProcessLimboHandler> weak_factory_;
};

}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_PROCESS_LIMBO_MANAGER_H_
