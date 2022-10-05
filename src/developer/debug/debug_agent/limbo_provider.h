// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_LIMBO_PROVIDER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_LIMBO_PROVIDER_H_

#include <lib/fit/result.h>

#include <map>
#include <vector>

#include "src/developer/debug/debug_agent/exception_handle.h"
#include "src/developer/debug/debug_agent/process_handle.h"
#include "src/developer/debug/debug_agent/thread_handle.h"
#include "src/developer/debug/shared/status.h"

namespace debug_agent {

// In charge of providing access to the ProcessLimbo.
//
// Fuchsia can be configured to keep processes that have excepted in a suspension state, called
// Limbo. This provides the possibility for debuggers to attach to those process way after the
// exception occurred. We call this process Just In Time Debugging (JITD).
class LimboProvider {
 public:
  struct Record {
    std::unique_ptr<ProcessHandle> process;
    std::unique_ptr<ThreadHandle> thread;
  };

  // Used when taking over an exception from limbo. This adds on the exception to the normal process
  // and thread handles.
  struct RetrievedException : public Record {
    std::unique_ptr<ExceptionHandle> exception;
  };

  // Maps process koids to the corresponding records.
  using RecordMap = std::map<zx_koid_t, Record>;

  using OnEnterLimboCallback = fit::function<void(const Record&)>;

  // Limbo can fail to initialize (eg. failed to connect). There is no point querying an invalid
  // limbo provider, so callers should check for validity via Valid() before using it. If the limbo
  // is invalid, callers should either attempt to initialize again or create another limbo provider.
  LimboProvider() = default;
  LimboProvider(LimboProvider&&) = default;
  virtual ~LimboProvider() = default;

  // Callback to be called whenever new processes enter the connected limbo.
  // See |on_enter_limbo_| for more details.
  void set_on_enter_limbo(OnEnterLimboCallback cb) { on_enter_limbo_ = std::move(cb); }

  // Returns true if this limbo provider is set up properly.
  virtual bool Valid() const = 0;

  // Returns true if the process with the given koid is in limbo.
  virtual bool IsProcessInLimbo(zx_koid_t process_koid) const = 0;

  // Read-only access to the processes currently waiting in limbo.
  virtual const RecordMap& GetLimboRecords() const = 0;

  // Consumes the process in limbo.
  virtual fit::result<debug::Status, RetrievedException> RetrieveException(
      zx_koid_t process_koid) = 0;

  virtual debug::Status ReleaseProcess(zx_koid_t process_koid) = 0;

 protected:
  // Callback to be triggered whenever a new process enters the the limbo. Provides the list of
  // new processes that just entered the limbo on this event. |Limbo()| is up to date at the moment
  // of this callback.
  OnEnterLimboCallback on_enter_limbo_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_LIMBO_PROVIDER_H_
