// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_CHILD_LISTENER_H_
#define SRC_MODULAR_BIN_BASEMGR_CHILD_LISTENER_H_

#include <fuchsia/component/cpp/fidl.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

#include <memory>
#include <string>

#include "fuchsia/hardware/power/statecontrol/cpp/fidl.h"
#include "lib/async/cpp/task.h"
#include "lib/async/dispatcher.h"
#include "lib/fit/function.h"
#include "lib/stdcompat/string_view.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace modular {

// Max number of times to try to start a child component before rebooting the
// system.
constexpr size_t kMaxCrashRecoveryLimit = 3;

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
                const std::vector<cpp17::string_view>& child_names);

  // Start all child components as passed in the constructor. This method
  // will try to establish a connection with each child up to
  // |kMaxCrashRecoveryLimit| times. If this limit is reached then the system
  // will reboot using the connection to `fuchsia.hardware.power.statecontrol.Admin`
  // passed with |administrator|.
  void StartListening(fuchsia::hardware::power::statecontrol::Admin* administrator);

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
    ChildListenerImpl(std::string name, std::string path);

    fit::closure Connect(sys::ServiceDirectory* svc, async_dispatcher_t* dispatcher,
                         fit::function<void(zx_status_t)> on_error);

    const std::string& GetPath() const;

    const std::string& GetName() const;

   private:
    std::string name_;
    std::string path_;
    fuchsia::component::BinderPtr binder_;
    fxl::WeakPtrFactory<ChildListenerImpl> weak_factory_;
  };

  void ConnectToChild(size_t attempt, ChildListenerImpl* impl,
                      fuchsia::hardware::power::statecontrol::Admin* administrator);

  sys::ServiceDirectory* const svc_ = nullptr;      // Not owned.
  async_dispatcher_t* const dispatcher_ = nullptr;  // Not owned.
  // |ChildListenerImpl needs to be wrapped in a std::unique_ptr because
  // the type is neither moveable nor copyable.
  std::vector<std::unique_ptr<ChildListenerImpl>> impls_ = {};

  fxl::WeakPtrFactory<ChildListener> weak_factory_;
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_CHILD_LISTENER_H_
