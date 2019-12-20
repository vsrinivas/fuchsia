// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_LIMBO_PROVIDER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_LIMBO_PROVIDER_H_

#include <fuchsia/exception/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>

#include <map>
#include <optional>
#include <vector>

#include "src/lib/fxl/memory/weak_ptr.h"

namespace debug_agent {

// In charge of providing access to the ProcessLimbo.
//
// Fuchsia can be configured to keep processes that have excepted in a suspension state, called
// Limbo. This provides the possibility for debuggers to attach to those process way after the
// exception occurred. We call this process Just In Time Debugging (JITD).
class LimboProvider {
 public:
  using OnEnterLimboCallback =
      fit::function<void(std::vector<fuchsia::exception::ProcessExceptionMetadata>)>;

  explicit LimboProvider(std::shared_ptr<sys::ServiceDirectory> services);
  virtual ~LimboProvider();

  // Init must be called after constructing the object.
  // It will get the current state of the limbo being connected and then issue async calls to keep
  // that state updated. If the provider is valid, |limbo()| is guaranteed to be up to date.
  //
  // |error_handler| will only be used if |Init| was successful. Once the error handler has been
  // issued, this provider is considered invalid and Init should be called again.
  virtual zx_status_t Init();

  // Callback to be called whenever new processes enter the connected limbo.
  // See |on_enter_limbo_| for more details.
  void set_on_enter_limbo(OnEnterLimboCallback cb) { on_enter_limbo_ = std::move(cb); }

  // Limbo can fail to initialize (eg. failed to connect). There is no point querying an invalid
  // limbo provider, so callers should check for validity before using it. If the limbo is invalid,
  // callers should either attempt to initialize again or create another limbo provider.
  //
  // NOTE: a valid limbo provider might be inactive (see ProcessLimbo fidl declaration) and/or be
  //       empty (not have any processes waiting on an exception).
  virtual bool Valid() const;

  virtual const std::map<zx_koid_t, fuchsia::exception::ProcessExceptionMetadata>& Limbo() const;

  virtual zx_status_t RetrieveException(zx_koid_t process_koid,
                                        fuchsia::exception::ProcessException* out);

  virtual zx_status_t ReleaseProcess(zx_koid_t process_koid);

 protected:
  // Callback to be triggered whenever a new process enters the the limbo. Provides the list of
  // new processes that just entered the limbo on this event. |Limbo()| is up to date at the moment
  // of this callback.
  OnEnterLimboCallback on_enter_limbo_;

 private:
  void Reset();

  void WatchActive();
  void WatchLimbo();

  bool valid_ = false;

  // Because the Process Limbo uses hanging gets (async callbacks) and this class exposes a
  // synchronous inteface, we need to keep track of the current state in order to be able to
  // return it immediatelly.
  std::map<zx_koid_t, fuchsia::exception::ProcessExceptionMetadata> limbo_;

  bool is_limbo_active_ = false;

  fuchsia::exception::ProcessLimboPtr connection_;

  std::shared_ptr<sys::ServiceDirectory> services_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_LIMBO_PROVIDER_H_
