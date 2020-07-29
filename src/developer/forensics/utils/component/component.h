// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_COMPONENT_COMPONENT_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_COMPONENT_COMPONENT_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>

#include <memory>

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
  Component();

  async_dispatcher_t* Dispatcher();
  std::shared_ptr<sys::ServiceDirectory> Services();
  inspect::Node* InspectRoot();

  zx_status_t RunLoop();

  template <typename Interface>
  zx_status_t AddPublicService(::fidl::InterfaceRequestHandler<Interface> handler,
                               std::string service_name = Interface::Name_) {
    return context_->outgoing()->AddPublicService(std::move(handler), std::move(service_name));
  }

  // Returns true if this is the first time an instance of the current component has been started
  // since boot.
  bool IsFirstInstance() const;

 private:
  async::Loop loop_;
  std::unique_ptr<sys::ComponentContext> context_;
  sys::ComponentInspector inspector_;
  size_t instance_index_;
};

}  // namespace component
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_COMPONENT_COMPONENT_H_
