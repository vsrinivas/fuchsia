// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "garnet/bin/mdns/standalone/mdns_standalone.h"

#include "garnet/bin/mdns/service/mdns_names.h"
#include "garnet/bin/mdns/standalone/ioctl_interface_monitor.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"

namespace mdns {
namespace {

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& value) {
  if (value.size() == 0) {
    return os << "<empty>";
  }

  for (const T& element : value) {
    os << element << " ";
  }

  return os;
}

const fxl::TimeDelta kTrafficLoggingInterval = fxl::TimeDelta::FromSeconds(60);

}  // namespace

MdnsStandalone::MdnsStandalone(const std::string& host_name) {
  mdns_.Start(IoctlInterfaceMonitor::Create(), host_name);
  // mdns_.SetVerbose(true);

  mdns_.SubscribeToService("_fuchsia._tcp.", this);

  if (!mdns_.PublishServiceInstance("_fuchsia._tcp.", host_name, this)) {
    std::cout
        << "publication failed: the instance is already published locally\n";
  }

  LogTrafficAfterDelay();
}

MdnsStandalone::~MdnsStandalone() {}

void MdnsStandalone::LogTrafficAfterDelay() {
  fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [this]() { LogTrafficAfterDelay(); }, kTrafficLoggingInterval);
}

void MdnsStandalone::InstanceDiscovered(const std::string& service,
                                        const std::string& instance,
                                        const SocketAddress& v4_address,
                                        const SocketAddress& v6_address,
                                        const std::vector<std::string>& text) {
  std::cout << "discovered: " << service << " " << instance << " " << v4_address
            << " " << v6_address << " " << text << "\n";
}

void MdnsStandalone::InstanceChanged(const std::string& service,
                                     const std::string& instance,
                                     const SocketAddress& v4_address,
                                     const SocketAddress& v6_address,
                                     const std::vector<std::string>& text) {
  std::cout << "changed: " << service << " " << instance << " " << v4_address
            << " " << v6_address << " " << text << "\n";
}

void MdnsStandalone::InstanceLost(const std::string& service,
                                  const std::string& instance) {
  std::cout << "lost: " << service << " " << instance << "\n";
}

void MdnsStandalone::UpdatesComplete() {}

void MdnsStandalone::ReportSuccess(bool success) {
  if (success) {
    std::cout << "publication successful\n";
  } else {
    std::cout << "publication failed: the instance is already published on the "
                 "subnet\n";
  }
}

void MdnsStandalone::GetPublication(
    bool query,
    const std::string& subtype,
    const std::function<void(std::unique_ptr<Mdns::Publication>)>& callback) {
  FXL_DCHECK(callback);
  callback(Mdns::Publication::Create(IpPort::From_uint16_t(6666),
                                     {"some", "metadata"}));
}

}  // namespace mdns
