// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OVERNET_OVERNETSTACK_SERVICE_H_
#define SRC_CONNECTIVITY_OVERNET_OVERNETSTACK_SERVICE_H_

#include <fuchsia/overnet/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include "src/connectivity/overnet/overnetstack/overnet_app.h"

namespace overnetstack {

class Service final : public fuchsia::overnet::Overnet,
                      public OvernetApp::Actor {
 public:
  Service(OvernetApp* app);

  overnet::Status Start() override;

  //////////////////////////////////////////////////////////////////////////////////////////////////
  // Method implementations

  void ListPeers(uint64_t last_seen_version,
                 ListPeersCallback callback) override;
  void RegisterService(std::string service_name,
                       fidl::InterfaceHandle<fuchsia::overnet::ServiceProvider>
                           provider) override;
  void ConnectToService(fuchsia::overnet::protocol::NodeId node,
                        std::string service_name, zx::channel channel) override;

 private:
  OvernetApp* const app_;
  fidl::BindingSet<fuchsia::overnet::Overnet> bindings_;
};

}  // namespace overnetstack

#endif  // SRC_CONNECTIVITY_OVERNET_OVERNETSTACK_SERVICE_H_
