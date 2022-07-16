// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_SERVICE_INSTANCE_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_SERVICE_INSTANCE_H_

#include <memory>

#include "src/lib/inet/socket_address.h"

namespace mdns {

struct ServiceInstance {
  static std::unique_ptr<ServiceInstance> Create(
      const std::string& service_name, const std::string& instance_name,
      const std::string& target_name, const std::vector<inet::SocketAddress>& addresses,
      const std::vector<std::vector<uint8_t>>& text = std::vector<std::vector<uint8_t>>(),
      uint16_t srv_priority = 0, uint16_t srv_weight = 0);

  ServiceInstance(
      const std::string& service_name, const std::string& instance_name,
      const std::string& target_name, const std::vector<inet::SocketAddress>& addresses,
      const std::vector<std::vector<uint8_t>>& text = std::vector<std::vector<uint8_t>>(),
      uint16_t srv_priority = 0, uint16_t srv_weight = 0);

  ServiceInstance() = default;

  std::unique_ptr<ServiceInstance> Clone() const;

  bool operator==(const ServiceInstance& other) const;

  std::string service_name_;
  std::string instance_name_;
  std::string target_name_;
  std::vector<inet::SocketAddress> addresses_;
  std::vector<std::vector<uint8_t>> text_;
  uint16_t srv_priority_ = 0;
  uint16_t srv_weight_ = 0;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_SERVICE_INSTANCE_H_
