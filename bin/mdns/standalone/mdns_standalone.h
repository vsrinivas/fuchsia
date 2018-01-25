// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/mdns/service/mdns.h"
#include "lib/fxl/macros.h"

namespace mdns {

class MdnsStandalone : public Mdns::Subscriber, public Mdns::Publisher {
 public:
  MdnsStandalone(const std::string& host_name);

  ~MdnsStandalone();

 private:
  void LogTrafficAfterDelay();

  // Mdns::Subscriber implementation:
  void InstanceDiscovered(const std::string& service,
                          const std::string& instance,
                          const SocketAddress& v4_address,
                          const SocketAddress& v6_address,
                          const std::vector<std::string>& text) override;

  void InstanceChanged(const std::string& service,
                       const std::string& instance,
                       const SocketAddress& v4_address,
                       const SocketAddress& v6_address,
                       const std::vector<std::string>& text) override;

  void InstanceLost(const std::string& service,
                    const std::string& instance) override;

  void UpdatesComplete() override;

  // Mdns::Publisher implementation:
  void ReportSuccess(bool success) override;

  void GetPublication(
      bool query,
      const std::string& subtype,
      const std::function<void(std::unique_ptr<Mdns::Publication>)>& callback)
      override;

  mdns::Mdns mdns_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MdnsStandalone);
};

}  // namespace mdns
