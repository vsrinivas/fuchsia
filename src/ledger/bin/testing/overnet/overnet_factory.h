// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_OVERNET_OVERNET_FACTORY_H_
#define SRC_LEDGER_BIN_TESTING_OVERNET_OVERNET_FACTORY_H_

#include <fuchsia/overnet/cpp/fidl.h>
#include <lib/fit/function.h>

#include "src/ledger/bin/fidl_helpers/bound_interface.h"
#include "src/ledger/bin/testing/data_generator.h"
#include "src/ledger/bin/testing/overnet/fake_overnet.h"
#include "src/lib/callback/auto_cleanable.h"

namespace ledger {

// OvernetFactory creates and manages connections to |FakeOvernet|s.
// It can be used to test the behavior of multiple Overnet clients without
// a multi-device setup.
class OvernetFactory : public FakeOvernet::Delegate {
 public:
  // If set to true, host lists of one host are not returned at all. This is a workaround for OV-8.
  OvernetFactory(async_dispatcher_t* dispatcher, bool return_one_host_list = false);
  OvernetFactory(const OvernetFactory&) = delete;
  OvernetFactory& operator=(const OvernetFactory&) = delete;
  ~OvernetFactory() override;

  // Creates a new virtual host with the given host name, and connects to its
  // Overnet.
  void AddBinding(uint64_t node_id, fidl::InterfaceRequest<fuchsia::overnet::Overnet> request);

 private:
  // Holder holds a Overnet.
  class Holder;

  // UpdatedHostList is called when the list of hosts changes. It sends
  // notifications to waiting clients as necessary.
  void UpdatedHostList();

  // Returns the list of hosts.
  std::vector<FakeOvernet::Delegate::FakePeer> MakeHostList();

  // FakeOvernet::Delegate:
  void ListPeers(uint64_t last_version,
                 fit::function<void(uint64_t, std::vector<FakeOvernet::Delegate::FakePeer>)>
                     callback) override;
  void ConnectToService(fuchsia::overnet::protocol::NodeId device_name, std::string service_name,
                        zx::channel channel) override;

  async_dispatcher_t* dispatcher_;

  void ServiceWasRegistered() override;

  // If set to true, host lists of one host are not returned at all. This is a workaround for OV-8.
  const bool return_one_host_list_;
  // Counter incremented each time a Overnet is added or removed; denotes
  // the version of the current device list.
  uint64_t current_version_ = 0;
  std::vector<fit::function<void(uint64_t, std::vector<FakeOvernet::Delegate::FakePeer>)>>
      pending_device_list_callbacks_;
  callback::AutoCleanableMap<uint64_t, Holder> net_connectors_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_OVERNET_OVERNET_FACTORY_H_
