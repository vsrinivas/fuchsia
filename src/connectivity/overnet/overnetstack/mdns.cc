// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/overnetstack/mdns.h"

#include <fbl/ref_counted.h>
#include <fuchsia/mdns/cpp/fidl.h>

#include "garnet/public/lib/fostr/fidl/fuchsia/mdns/formatting.h"
#include "src/connectivity/overnet/lib/labels/node_id.h"
#include "src/connectivity/overnet/overnetstack/fuchsia_port.h"

namespace overnetstack {

static const char* kServiceName = "_temp_overnet._udp.";

static fuchsia::mdns::ControllerPtr Connect(
    sys::ComponentContext* component_context, const char* why) {
  auto svc = component_context->svc()->Connect<fuchsia::mdns::Controller>();
  svc.set_error_handler([why](zx_status_t status) {
    FXL_LOG(DFATAL) << why << " mdns failure: " << zx_status_get_string(status);
  });
  return svc;
}

class MdnsIntroducer::Impl : public fbl::RefCounted<MdnsIntroducer>,
                             public fuchsia::mdns::ServiceSubscriber {
 public:
  Impl(UdpNub* nub) : nub_(nub), subscriber_binding_(this) {}

  void Begin(sys::ComponentContext* component_context) {
    std::cerr << "Querying mDNS for overnet services [" << kServiceName
              << "]\n";
    auto svc = Connect(component_context, "Introducer");
    fidl::InterfaceHandle<fuchsia::mdns::ServiceSubscriber> subscriber_handle;

    subscriber_binding_.Bind(subscriber_handle.NewRequest());
    subscriber_binding_.set_error_handler([this](zx_status_t status) {
      subscriber_binding_.set_error_handler(nullptr);
      subscriber_binding_.Unbind();
    });

    svc->SubscribeToService(kServiceName, std::move(subscriber_handle));
  }

 private:
  struct ServiceInstance {
    ServiceInstance(std::vector<std::string> t,
                    std::vector<fuchsia::netstack::SocketAddress> a)
        : text(std::move(t)), addresses(std::move(a)) {}
    std::vector<std::string> text;
    std::vector<fuchsia::netstack::SocketAddress> addresses;
  };
  using ServiceMap = std::map<overnet::NodeId, ServiceInstance>;

  void HandleDiscoverOrUpdate(const fuchsia::mdns::ServiceInstance& svc,
                              bool update) {
    if (svc.service_name != kServiceName) {
      std::cout << "Unexpected service name (ignored): " << svc.service_name
                << "\n";
      return;
    }
    auto parsed_instance_name = overnet::NodeId::FromString(svc.instance_name);
    if (parsed_instance_name.is_error()) {
      std::cout << "Failed to parse instance name: "
                << parsed_instance_name.AsStatus() << "\n";
      return;
    }
    auto instance_id = *parsed_instance_name.get();
    auto it = service_map_.find(instance_id);
    if ((it != service_map_.end()) != update) {
      if (update) {
        std::cout << "WARNING: Update for unknown instance " << instance_id
                  << "; ignoring\n";
      } else {
        std::cout << "WARNING: Discovery of known instance " << instance_id
                  << "; ignoring\n";
      }
      return;
    }
    std::vector<fuchsia::netstack::SocketAddress> addresses;
    if (svc.v4_address) {
      addresses.emplace_back();
      auto result =
          overnet::Status::FromZx(svc.v4_address->Clone(&addresses.back()));
      if (result.is_error()) {
        std::cout << "Failed to clone v4_address: " << result << "\n";
        addresses.pop_back();
      }
    }
    if (svc.v6_address) {
      addresses.emplace_back();
      auto result =
          overnet::Status::FromZx(svc.v6_address->Clone(&addresses.back()));
      if (result.is_error()) {
        std::cout << "Failed to clone v6_address: " << result << "\n";
        addresses.pop_back();
      }
    }
    std::vector<std::string> text;
    for (const auto& line : svc.text) {
      text.push_back(line);
    }

    if (it == service_map_.end()) {
      NewConnection(instance_id, addresses);
      service_map_.emplace(
          std::piecewise_construct, std::forward_as_tuple(instance_id),
          std::forward_as_tuple(std::move(text), std::move(addresses)));
    } else if (it->second.addresses != addresses) {
      NewConnection(instance_id, addresses);
      it->second.addresses = std::move(addresses);
    }
  }

  void NewConnection(
      overnet::NodeId node_id,
      const std::vector<fuchsia::netstack::SocketAddress>& addresses) {
    for (const auto& addr : addresses) {
      auto status =
          ToUdpAddr(addr).Then([node_id, nub = nub_](const UdpAddr& addr) {
            std::cerr << "Initiating connection to: " << node_id << " at "
                      << addr << "\n";
            nub->Initiate(addr, node_id);
            return overnet::Status::Ok();
          });
      if (status.is_error()) {
        std::cerr << "Failed to initiate connection: " << status << "\n";
      }
    }
  }

  static overnet::StatusOr<UdpAddr> ToUdpAddr(
      const fuchsia::netstack::SocketAddress& sock_addr) {
    const fuchsia::net::IpAddress& net_addr = sock_addr.addr;
    UdpAddr udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    switch (net_addr.Which()) {
      case fuchsia::net::IpAddress::Tag::Invalid:
        return overnet::Status(overnet::StatusCode::INVALID_ARGUMENT,
                               "unknown address type");
      case fuchsia::net::IpAddress::Tag::kIpv4:
        if (!net_addr.is_ipv4()) {
          return overnet::Status(overnet::StatusCode::INVALID_ARGUMENT,
                                 "bad ipv4 address");
        }
        udp_addr.ipv4.sin_family = AF_INET;
        udp_addr.ipv4.sin_port = htons(sock_addr.port);
        memcpy(&udp_addr.ipv4.sin_addr, net_addr.ipv4().addr.data(),
               sizeof(udp_addr.ipv4.sin_addr));
        return udp_addr;
      case fuchsia::net::IpAddress::Tag::kIpv6:
        if (!net_addr.is_ipv6()) {
          return overnet::Status(overnet::StatusCode::INVALID_ARGUMENT,
                                 "bad ipv6 address");
        }
        udp_addr.ipv6.sin6_family = AF_INET6;
        udp_addr.ipv6.sin6_port = htons(sock_addr.port);
        memcpy(&udp_addr.ipv6.sin6_addr, net_addr.ipv6().addr.data(),
               sizeof(udp_addr.ipv6.sin6_addr));
        return udp_addr;
    }
    return overnet::Status(overnet::StatusCode::INVALID_ARGUMENT,
                           "bad address family");
  }

  // fuchsia::mdns::ServiceSubscriber implementation.
  void InstanceDiscovered(fuchsia::mdns::ServiceInstance instance,
                          InstanceDiscoveredCallback callback) {
    HandleDiscoverOrUpdate(instance, false);
    callback();
  }

  void InstanceChanged(fuchsia::mdns::ServiceInstance instance,
                       InstanceChangedCallback callback) {
    HandleDiscoverOrUpdate(instance, true);
    callback();
  }

  void InstanceLost(std::string service_name, std::string instance_name,
                    InstanceLostCallback callback) {
    callback();
  }

  UdpNub* const nub_;
  fidl::Binding<fuchsia::mdns::ServiceSubscriber> subscriber_binding_;
  ServiceMap service_map_;
};

MdnsIntroducer::MdnsIntroducer(OvernetApp* app, UdpNub* udp_nub)
    : app_(app), udp_nub_(udp_nub) {}

overnet::Status MdnsIntroducer::Start() {
  auto impl = fbl::MakeRefCounted<Impl>(udp_nub_);
  impl_ = std::move(impl);
  impl_->Begin(app_->component_context());
  return overnet::Status::Ok();
}

MdnsIntroducer::~MdnsIntroducer() {}

class MdnsAdvertisement::Impl {
 public:
  Impl(sys::ComponentContext* component_context, UdpNub* nub)
      : controller_(Connect(component_context, "Advertisement")),
        node_id_(nub->node_id()) {
    std::cerr << "Requesting mDNS advertisement for " << node_id_ << " on port "
              << nub->port() << "\n";
    controller_->DEPRECATEDPublishServiceInstance(
        kServiceName, node_id_.ToString(), nub->port(), {}, true,
        [node_id = node_id_, port = nub->port()](fuchsia::mdns::Result result) {
          std::cout << "Advertising " << node_id << " on port " << port
                    << " via mdns gets: " << result << "\n";
        });
  }
  ~Impl() {
    controller_->DEPRECATEDUnpublishServiceInstance(kServiceName,
                                                    node_id_.ToString());
  }

 private:
  const fuchsia::mdns::ControllerPtr controller_;
  const overnet::NodeId node_id_;
};

MdnsAdvertisement::MdnsAdvertisement(OvernetApp* app, UdpNub* udp_nub)
    : app_(app), udp_nub_(udp_nub) {}

overnet::Status MdnsAdvertisement::Start() {
  impl_.reset(new Impl(app_->component_context(), udp_nub_));
  return overnet::Status::Ok();
}

MdnsAdvertisement::~MdnsAdvertisement() {}

}  // namespace overnetstack
