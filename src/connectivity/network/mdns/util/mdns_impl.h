// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_UTIL_MDNS_IMPL_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_UTIL_MDNS_IMPL_H_

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>

#include "src/connectivity/network/mdns/util/commands.h"
#include "src/lib/fsl/tasks/fd_waiter.h"
#include "src/lib/line_input/modal_line_input.h"

namespace mdns {

class MdnsImpl {
 public:
  MdnsImpl(sys::ComponentContext* component_context, Command command,
           async_dispatcher_t* dispatcher, fit::closure quit_callback);

  ~MdnsImpl() = default;

 private:
  class HostNameSubscriptionListener : public fuchsia::net::mdns::HostNameSubscriptionListener {
   public:
    HostNameSubscriptionListener(
        std::string host_name,
        fidl::InterfaceRequest<fuchsia::net::mdns::HostNameSubscriptionListener> request,
        line_input::ModalLineInput& input);

    ~HostNameSubscriptionListener() override = default;

    void set_error_handler(fit::function<void(zx_status_t)> error_handler);

    // fuchsia::net::mdns::HostNameSubscriptionListener implementation.
    void OnAddressesChanged(std::vector<fuchsia::net::mdns::HostAddress> addresses,
                            OnAddressesChangedCallback callback) override;

   private:
    std::string host_name_;
    fidl::Binding<fuchsia::net::mdns::HostNameSubscriptionListener> binding_;
    line_input::ModalLineInput& input_;
  };

  class ServiceSubscriptionListener : public fuchsia::net::mdns::ServiceSubscriptionListener {
   public:
    ServiceSubscriptionListener(
        const std::string& service_name,
        fidl::InterfaceRequest<fuchsia::net::mdns::ServiceSubscriptionListener> request,
        line_input::ModalLineInput& input);

    ~ServiceSubscriptionListener() override = default;

    void set_error_handler(fit::function<void(zx_status_t)> error_handler);

    // fuchsia::net::mdns::ServiceSubscriptionListener implementation.
    void OnInstanceDiscovered(fuchsia::net::mdns::ServiceInstance instance,
                              OnInstanceDiscoveredCallback callback) override;

    void OnInstanceChanged(fuchsia::net::mdns::ServiceInstance instance,
                           OnInstanceChangedCallback callback) override;

    void OnInstanceLost(std::string service_name, std::string instance_name,
                        OnInstanceLostCallback callback) override;

    void OnQuery(fuchsia::net::mdns::ResourceType resource_type, OnQueryCallback callback) override;

   private:
    std::string service_name_;
    fidl::Binding<fuchsia::net::mdns::ServiceSubscriptionListener> binding_;
    line_input::ModalLineInput& input_;
  };

  class ServiceInstancePublicationResponder
      : public fuchsia::net::mdns::ServiceInstancePublicationResponder {
   public:
    ServiceInstancePublicationResponder(
        fidl::InterfaceRequest<fuchsia::net::mdns::ServiceInstancePublicationResponder> request,
        line_input::ModalLineInput& input,
        fuchsia::net::mdns::ServiceInstancePublication publication);

    ~ServiceInstancePublicationResponder() override = default;

    void set_error_handler(fit::function<void(zx_status_t)> error_handler);

    // fuchsia::net::mdns::ServiceInstancePublicationResponder implementation.
    void OnPublication(fuchsia::net::mdns::ServiceInstancePublicationCause publication_cause,
                       fidl::StringPtr subtype,
                       std::vector<fuchsia::net::IpAddress> source_addresses,
                       OnPublicationCallback callback) override;

   private:
    fidl::Binding<fuchsia::net::mdns::ServiceInstancePublicationResponder> binding_;
    line_input::ModalLineInput& input_;
    fuchsia::net::mdns::ServiceInstancePublication publication_;
  };

  class ProxyHost {
   public:
    explicit ProxyHost(fuchsia::net::mdns::ServiceInstancePublisherPtr publisher);

    ~ProxyHost() = default;

    void set_error_handler(fit::function<void(zx_status_t)> error_handler);

    fuchsia::net::mdns::ServiceInstancePublisherPtr& publisher() { return publisher_; }

    bool ResponderExists(std::string instance_full_name);

    void AddResponder(std::string instance_full_name,
                      std::unique_ptr<ServiceInstancePublicationResponder> responder);

    bool RemoveResponder(std::string instance_full_name);

   private:
    fuchsia::net::mdns::ServiceInstancePublisherPtr publisher_;
    std::unordered_map<std::string, std::unique_ptr<ServiceInstancePublicationResponder>>
        service_instance_publication_responders_by_instance_full_name_;
  };

  void ExecuteCommand(const Command& command);

  void WaitForKeystroke();

  void ResolveHost(const std::string& host_name, zx::duration timeout,
                   fuchsia::net::mdns::Media media, fuchsia::net::mdns::IpVersions ip_versions,
                   bool exclude_local, bool exclude_local_proxies);

  void ResolveInstance(const std::string& instance_name, const std::string& service_name,
                       zx::duration timeout, fuchsia::net::mdns::Media media,
                       fuchsia::net::mdns::IpVersions ip_versions, bool exclude_local,
                       bool exclude_local_proxies);

  void SubscribeHost(const std::string& host_name, fuchsia::net::mdns::Media media,
                     fuchsia::net::mdns::IpVersions ip_versions, bool exclude_local,
                     bool exclude_local_proxies);

  void SubscribeService(const std::string& service_name, fuchsia::net::mdns::Media media,
                        fuchsia::net::mdns::IpVersions ip_versions, bool exclude_local,
                        bool exclude_local_proxies);

  void PublishHost(const std::string& host_name, std::vector<inet::IpAddress> addresses, bool probe,
                   fuchsia::net::mdns::Media media, fuchsia::net::mdns::IpVersions ip_versions);

  void PublishInstance(const std::string& instance_name, const std::string& service_name,
                       uint16_t port, const std::vector<std::string>& text, bool probe,
                       fuchsia::net::mdns::Media media, fuchsia::net::mdns::IpVersions ip_versions,
                       uint16_t srv_priority, uint16_t srv_weight, zx::duration ptr_ttl,
                       zx::duration srv_ttl, zx::duration txt_ttl,
                       const std::string& proxy_host_name);

  void UnsubscribeHost(const std::string& host_name);

  void UnsubscribeService(const std::string& service_name);

  void UnpublishHost(const std::string& host_name);

  void UnpublishInstance(const std::string& instance_name, const std::string& service_name,
                         const std::string& proxy_host_name);

  void EnsureHostNameResolver();

  void EnsureServiceInstanceResolver();

  void EnsureHostNameSubscriber();

  void EnsureServiceSubscriber();

  void EnsureProxyHostPublisher();

  void EnsureServiceInstancePublisher();

  // Calls |QuitSync| in a deferred task.
  void Quit();

  // Quits synchronously (actually deletes *this).
  void QuitSync();

  // Calls |Quit| if transient.
  void QuitIfTransient();

  // Shows the input line if not transient.
  void ShowInput();

  // Hides the input line if not transient.
  void HideInput();

  sys::ComponentContext* component_context_;
  async_dispatcher_t* dispatcher_;
  fit::closure quit_callback_;
  bool transient_ = true;
  line_input::ModalLineInput input_;

  fuchsia::net::mdns::HostNameResolverPtr host_name_resolver_;
  fuchsia::net::mdns::ServiceInstanceResolverPtr service_instance_resolver_;
  fuchsia::net::mdns::HostNameSubscriberPtr host_name_subscriber_;
  fuchsia::net::mdns::ServiceSubscriber2Ptr service_subscriber_;
  fuchsia::net::mdns::ProxyHostPublisherPtr proxy_host_publisher_;
  fuchsia::net::mdns::ServiceInstancePublisherPtr service_instance_publisher_;

  std::unordered_map<std::string, std::unique_ptr<HostNameSubscriptionListener>>
      host_name_subscription_listeners_by_host_name_;
  std::unordered_map<std::string, std::unique_ptr<ServiceSubscriptionListener>>
      service_subscription_listeners_by_service_name_;
  std::unordered_map<std::string, std::unique_ptr<ProxyHost>> proxy_hosts_by_name_;
  std::unordered_map<std::string, std::unique_ptr<ServiceInstancePublicationResponder>>
      service_instance_publication_responders_by_instance_full_name_;

  fsl::FDWaiter fd_waiter_;

  // Disallow copy, assign and move.
  MdnsImpl(const MdnsImpl&) = delete;
  MdnsImpl(MdnsImpl&&) = delete;
  MdnsImpl& operator=(const MdnsImpl&) = delete;
  MdnsImpl& operator=(MdnsImpl&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_UTIL_MDNS_IMPL_H_
