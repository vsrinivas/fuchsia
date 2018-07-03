// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_NETCONNECTOR_NETCONNECTOR_FACTORY_H_
#define PERIDOT_BIN_LEDGER_TESTING_NETCONNECTOR_NETCONNECTOR_FACTORY_H_

#include <fuchsia/netconnector/cpp/fidl.h>
#include <lib/callback/auto_cleanable.h>
#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/fidl_helpers/bound_interface.h"
#include "peridot/bin/ledger/testing/data_generator.h"
#include "peridot/bin/ledger/testing/netconnector/fake_netconnector.h"

namespace ledger {

// NetConnectorFactory creates and manages connections to |FakeNetConnector|s.
// It can be used to test the behavior of multiple NetConnector clients without
// a multi-device setup.
class NetConnectorFactory : public FakeNetConnector::Delegate {
 public:
  NetConnectorFactory();
  ~NetConnectorFactory() override;

  // Creates a new virtual host with the given host name, and connects to its
  // NetConnector.
  void AddBinding(
      std::string host_name,
      fidl::InterfaceRequest<fuchsia::netconnector::NetConnector> request);

 private:
  // Holder holds a NetConnector.
  class Holder;

  // UpdatedHostList is called when the list of hosts changes. It sends
  // notifications to waiting clients as necessary.
  void UpdatedHostList();

  // FakeNetConnector::Delegate:
  void GetDevicesNames(
      uint64_t last_version,
      fit::function<void(uint64_t, fidl::VectorPtr<fidl::StringPtr>)> callback)
      override;
  void ConnectToServiceProvider(
      std::string device_name,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request) override;

  // Counter incremented each time a NetConnector is added or removed; denotes
  // the version of the current device list.
  uint64_t current_version_ = 0;
  std::vector<fit::function<void(uint64_t, fidl::VectorPtr<fidl::StringPtr>)>>
      pending_device_list_callbacks_;
  callback::AutoCleanableMap<std::string, Holder> net_connectors_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NetConnectorFactory);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTING_NETCONNECTOR_NETCONNECTOR_FACTORY_H_
