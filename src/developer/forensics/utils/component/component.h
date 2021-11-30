// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_COMPONENT_COMPONENT_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_COMPONENT_COMPONENT_H_

#include <fuchsia/process/lifecycle/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "src/lib/timekeeper/clock.h"
#include "src/lib/timekeeper/system_clock.h"

namespace forensics {
namespace component {

// Forensics components all use the same basic machinery to function. Component groups that
// machinery together and provides some additional information about the component instance that has
// been started.
//
// To properly use this class a component must have access to the "isolated-temp" feature in its
// sandbox and all instances of the component must have non-overlapping lifetimes and share the same
// namespace.
class Component {
 public:
  // Set |lazy_outgoing_dir| to true if the component should wait to publish its outgoing directory
  // until the first call to |AddPublicService|.
  explicit Component(bool lazy_outgoing_dir = false);

  async_dispatcher_t* Dispatcher();
  std::shared_ptr<sys::ServiceDirectory> Services();
  inspect::Node* InspectRoot();
  timekeeper::Clock* Clock();

  zx_status_t RunLoop();
  void ShutdownLoop();

  template <typename Interface>
  zx_status_t AddPublicService(::fidl::InterfaceRequestHandler<Interface> handler,
                               std::string service_name = Interface::Name_) {
    if (!serving_outgoing_) {
      FX_LOGS(INFO) << "Serving outgoing directory";
      context_->outgoing()->ServeFromStartupInfo(dispatcher_);
      serving_outgoing_ = true;
    }

    return context_->outgoing()->AddPublicService(std::move(handler), std::move(service_name));
  }

  // Returns true if this is the first time an instance of the current component has been started
  // since boot.
  bool IsFirstInstance() const;

  // Handle stopping the component when the Stop signal is received. The parent will be notified
  // that it can stop the component when |deferred_callback| is executed.
  //
  // Note: This will start serving the outgoing directory if |lazy_outgoing_dir| was set to true.
  void OnStopSignal(
      ::fidl::InterfaceRequest<fuchsia::process::lifecycle::Lifecycle> lifecycle_channel,
      ::fit::function<void(::fit::deferred_callback)> on_stop);

 protected:
  // Constructor for testing when the component should run on a different loop than |loop_|.
  Component(async_dispatcher_t* dispatcher, std::unique_ptr<sys::ComponentContext> context,
            bool serving_outgoing);

 private:
  size_t InitialInstanceIndex() const;
  void WriteInstanceIndex() const;

  async::Loop loop_;
  async_dispatcher_t* dispatcher_;
  std::unique_ptr<sys::ComponentContext> context_;
  sys::ComponentInspector inspector_;
  timekeeper::SystemClock clock_;
  size_t instance_index_;

  bool serving_outgoing_;

  std::unique_ptr<fuchsia::process::lifecycle::Lifecycle> lifecycle_;
  std::unique_ptr<::fidl::Binding<fuchsia::process::lifecycle::Lifecycle>> lifecycle_connection_;
};

}  // namespace component
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_COMPONENT_COMPONENT_H_
