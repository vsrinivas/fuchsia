// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_UTIL_MDNS_IMPL_H_
#define GARNET_BIN_MDNS_UTIL_MDNS_IMPL_H_

#include <fuchsia/mdns/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>

#include "garnet/bin/mdns/util/mdns_params.h"
#include "lib/fsl/tasks/fd_waiter.h"

namespace mdns {

class MdnsImpl : public fuchsia::mdns::Responder,
                 public fuchsia::mdns::ServiceSubscriber {
 public:
  MdnsImpl(sys::ComponentContext* component_context, MdnsParams* params,
           fit::closure quit_callback);

  ~MdnsImpl() override;

 private:
  void WaitForKeystroke();

  void HandleKeystroke();

  void Resolve(const std::string& host_name, uint32_t timeout_seconds);

  void Subscribe(const std::string& service_name);

  void Publish(const std::string& service_name,
               const std::string& instance_name, uint16_t port,
               const std::vector<std::string>& text);

  void Unpublish(const std::string& service_name,
                 const std::string& instance_name);

  void Respond(const std::string& service_name,
               const std::string& instance_name, uint16_t port,
               const std::vector<std::string>& announce,
               const std::vector<std::string>& text);

  void UpdateStatus(fuchsia::mdns::Result result);

  // fuchsia::mdns::Responder implementation.
  void GetPublication(bool query, fidl::StringPtr subtype,
                      GetPublicationCallback callback) override;

  // fuchsia::mdns::ServiceSubscriber implementation.
  void InstanceDiscovered(fuchsia::mdns::ServiceInstance instance,
                          InstanceDiscoveredCallback callback) override;

  void InstanceChanged(fuchsia::mdns::ServiceInstance instance,
                       InstanceChangedCallback callback) override;

  void InstanceLost(std::string service_name, std::string instance_name,
                    InstanceLostCallback callback) override;

  fit::closure quit_callback_;
  fuchsia::mdns::ControllerPtr controller_;
  fidl::Binding<fuchsia::mdns::Responder> responder_binding_;
  fidl::Binding<fuchsia::mdns::ServiceSubscriber> subscriber_binding_;
  fsl::FDWaiter fd_waiter_;

  uint16_t publication_port_;
  std::vector<std::string> publication_text_;

  // Disallow copy, assign and move.
  MdnsImpl(const MdnsImpl&) = delete;
  MdnsImpl(MdnsImpl&&) = delete;
  MdnsImpl& operator=(const MdnsImpl&) = delete;
  MdnsImpl& operator=(MdnsImpl&&) = delete;
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_UTIL_MDNS_IMPL_H_
