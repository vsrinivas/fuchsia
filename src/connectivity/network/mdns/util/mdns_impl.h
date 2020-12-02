// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_UTIL_MDNS_IMPL_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_UTIL_MDNS_IMPL_H_

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>

#include "src/connectivity/network/mdns/util/mdns_params.h"
#include "src/lib/fsl/tasks/fd_waiter.h"

namespace mdns {

class MdnsImpl : public fuchsia::net::mdns::PublicationResponder,
                 public fuchsia::net::mdns::ServiceSubscriber {
 public:
  MdnsImpl(sys::ComponentContext* component_context, MdnsParams* params,
           fit::closure quit_callback);

  ~MdnsImpl() override;

 private:
  void WaitForKeystroke();

  void HandleKeystroke();

  void Resolve(const std::string& host_name, uint32_t timeout_seconds);

  void Subscribe(const std::string& service_name);

  void Respond(const std::string& service_name, const std::string& instance_name, uint16_t port,
               const std::vector<std::string>& announce, const std::vector<std::string>& text);

  void EnsureResolver();

  void EnsureSubscriber();

  void EnsurePublisher();

  void Quit();

  // fuchsia::net::mdns::PublicationResponder implementation.
  void OnPublication(fuchsia::net::mdns::PublicationCause publication_cause,
                     fidl::StringPtr subtype, std::vector<fuchsia::net::IpAddress> source_addresses,
                     OnPublicationCallback callback) override;

  // fuchsia::net::mdns::ServiceSubscriber implementation.
  void OnInstanceDiscovered(fuchsia::net::mdns::ServiceInstance instance,
                            OnInstanceDiscoveredCallback callback) override;

  void OnInstanceChanged(fuchsia::net::mdns::ServiceInstance instance,
                         OnInstanceChangedCallback callback) override;

  void OnInstanceLost(std::string service_name, std::string instance_name,
                      OnInstanceLostCallback callback) override;

  void OnQuery(fuchsia::net::mdns::ResourceType resource_type, OnQueryCallback callback) override;

  sys::ComponentContext* component_context_;
  fit::closure quit_callback_;
  fuchsia::net::mdns::ResolverPtr resolver_;
  fuchsia::net::mdns::SubscriberPtr subscriber_;
  fuchsia::net::mdns::PublisherPtr publisher_;
  fidl::Binding<fuchsia::net::mdns::PublicationResponder> responder_binding_;
  fidl::Binding<fuchsia::net::mdns::ServiceSubscriber> subscriber_binding_;
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

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_UTIL_MDNS_IMPL_H_
