// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_CHILD_LISTENER_H_
#define SRC_MODULAR_BIN_BASEMGR_CHILD_LISTENER_H_

#include <fuchsia/component/cpp/fidl.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

#include <memory>
#include <string>

#include "fuchsia/session/cpp/fidl.h"
#include "lib/async/cpp/task.h"
#include "lib/async/dispatcher.h"
#include "lib/fit/function.h"
#include "lib/stdcompat/string_view.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace modular {

// Max number of times to try to start an eager child component before giving
// up.
constexpr size_t kMaxCrashRecoveryLimit = 3;

// A child component in which basemgr should attempt to start.
struct Child {
  // The child name as written in this component's manifest.
  cpp17::string_view name;

  // Flag used to determine if this child is critical. If it is, then
  // basemgr will restart the session if the child fails to start 3 times.
  bool critical = false;
};

// |ChildListener| starts and monitor child components.
//
// The |child_names| passed to its constructor is used to establish a
// connection with the FIDL protocol `fuchsia.component.Binder`. The
// expectation is that the protocol is hosted under the path
// `fuchsia.component.Binder.<child_name>` in the provided |svc| directory.
//
// Neither |svc| nor |dispatcher| should be nullptr. If any
// of them are nullptr, this class will yield undefined behavior.
class ChildListener final {
 public:
  ChildListener(sys::ServiceDirectory* svc, async_dispatcher_t* dispatcher,
                const std::vector<Child>& children, size_t backoff_base,
                inspect::Node child_restart_tracker);

  // Start all child components as passed in the constructor. This method
  // will try to establish a connection with each child up to
  // |kMaxCrashRecoveryLimit| times. For children marked as critical, then the
  // session will restart using the connection to `fuchsia.session.Restarter`
  // passed with |session_restarter|.
  void StartListening(fuchsia::session::Restarter* session_restarter);

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ChildListener);

 private:
  // Implementation of connection to a single child component.
  class ChildListenerImpl {
   public:
    // |name| should be the name of the child component to connect to. This
    // is used for logging/debugging purposes.
    // |path| should be a path to the `fuchsia.component.Binder` FIDL protocol
    // in the ServiceDirectory associated with the enclosing |ChildListener|
    // object.
    // An empty |path| value will yield undefined behavior.
    // |num_restarts| is an Inspect value used to track restart attempts on this
    // child component. It should be set to 0 when passed to the constructor.
    // On each restart attempt, it will be incremented by 1.
    ChildListenerImpl(Child child, std::string path, inspect::UintProperty num_restarts);

    fit::closure Connect(sys::ServiceDirectory* svc, async_dispatcher_t* dispatcher,
                         fit::function<void(zx_status_t)> on_error);

    void IncrementRestartCount();

    const std::string& GetPath() const;

    cpp17::string_view GetName() const;

    bool IsCritical() const;

   private:
    Child child_;
    std::string path_;
    inspect::UintProperty num_restarts_;

    fuchsia::component::BinderPtr binder_;
    fxl::WeakPtrFactory<ChildListenerImpl> weak_factory_;
  };

  void ConnectToCriticalChild(ChildListenerImpl* impl,
                              fuchsia::session::Restarter* session_restarter);

  void ConnectToEagerChild(ChildListenerImpl* impl, size_t attempt);

  sys::ServiceDirectory* const svc_ = nullptr;      // Not owned.
  async_dispatcher_t* const dispatcher_ = nullptr;  // Not owned.
  size_t backoff_base_;
  inspect::Node child_restart_tracker_;
  // |ChildListenerImpl needs to be wrapped in a std::unique_ptr because
  // the type is neither moveable nor copyable.
  std::vector<std::unique_ptr<ChildListenerImpl>> impls_ = {};

  fxl::WeakPtrFactory<ChildListener> weak_factory_;
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_CHILD_LISTENER_H_
