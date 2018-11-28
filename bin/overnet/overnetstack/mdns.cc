// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mdns.h"
#include <fuchsia/mdns/cpp/fidl.h>
#include "fuchsia_port.h"
#include "garnet/lib/overnet/node_id.h"
#include "garnet/public/lib/fostr/fidl/fuchsia/mdns/formatting.h"

namespace overnetstack {

static const char* kServiceName =
    "__overnet__mdns__test__1db2_6473_a3b1_500c__._udp.";

class MdnsIntroducer : public fbl::RefCounted<MdnsIntroducer> {
 public:
  MdnsIntroducer(UdpNub* nub) : nub_(nub) {}

  void Begin(component::StartupContext* startup_context) {
    std::cerr << "Querying mDNS for overnet services [" << kServiceName
              << "]\n";
    auto svc = startup_context
                   ->ConnectToEnvironmentService<fuchsia::mdns::MdnsService>();
    svc->SubscribeToService(kServiceName, subscription_.NewRequest());
    RunLoop(0);
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

  void RunLoop(uint64_t version) {
    subscription_->GetInstances(
        version,
        [self = fbl::RefPtr<MdnsIntroducer>(this)](
            uint64_t new_version,
            fidl::VectorPtr<fuchsia::mdns::MdnsServiceInstance> services) {
          // Convert list of services into a service map.
          ServiceMap new_service_map;
          if (!services.is_null()) {
            for (const auto& svc : services.get()) {
              if (svc.service_name != kServiceName) {
                std::cout << "Unexpected service name (ignored): "
                          << svc.service_name << "\n";
                continue;
              }
              auto parsed_instance_name =
                  overnet::NodeId::FromString(svc.instance_name);
              if (parsed_instance_name.is_error()) {
                std::cout << "Failed to parse instance name: "
                          << parsed_instance_name.AsStatus() << "\n";
                continue;
              }
              auto instance_id = *parsed_instance_name.get();
              if (new_service_map.count(instance_id) != 0) {
                std::cout << "WARNING: Duplicate mdns definition for "
                          << instance_id << "; only using first\n";
                continue;
              }
              std::vector<fuchsia::netstack::SocketAddress> addresses;
              if (svc.v4_address) {
                addresses.emplace_back();
                auto result =
                    ToOvernetStatus(svc.v4_address->Clone(&addresses.back()));
                if (result.is_error()) {
                  std::cout << "Failed to clone v4_address: " << result << "\n";
                  addresses.pop_back();
                }
              }
              if (svc.v6_address) {
                addresses.emplace_back();
                auto result =
                    ToOvernetStatus(svc.v6_address->Clone(&addresses.back()));
                if (result.is_error()) {
                  std::cout << "Failed to clone v6_address: " << result << "\n";
                  addresses.pop_back();
                }
              }
              std::vector<std::string> text;
              if (!svc.text.is_null()) {
                for (const auto& line : svc.text.get()) {
                  text.push_back(line);
                }
              }
              new_service_map.emplace(
                  std::piecewise_construct, std::forward_as_tuple(instance_id),
                  std::forward_as_tuple(std::move(text), std::move(addresses)));
            }
          }

          // Compare new and old service maps and form new connections for any
          // newly advertised (or differently advertised) nodes.
          auto it_new = new_service_map.begin();
          auto it_old = self->last_result_.begin();
          const auto end_new = new_service_map.end();
          const auto end_old = self->last_result_.end();

          while (it_new != end_new && it_old != end_old) {
            if (it_new->first == it_old->first) {
              if (it_new->second.addresses != it_old->second.addresses) {
                self->NewConnection(it_new->first, it_new->second.addresses);
              }
              ++it_new;
              ++it_old;
            } else if (it_new->first < it_old->first) {
              self->NewConnection(it_new->first, it_new->second.addresses);
              ++it_new;
            } else {
              assert(it_old->first < it_new->first);
              ++it_old;
            }
          }
          while (it_new != end_new) {
            self->NewConnection(it_new->first, it_new->second.addresses);
            ++it_new;
          }

          // Record the current latest.
          self->last_result_.swap(new_service_map);

          // Check again.
          self->RunLoop(new_version);
        });
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
    const fuchsia::netstack::NetAddress& net_addr = sock_addr.addr;
    UdpAddr udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    switch (net_addr.family) {
      case fuchsia::netstack::NetAddressFamily::UNSPECIFIED:
        return overnet::Status(overnet::StatusCode::INVALID_ARGUMENT,
                               "unknown address type");
      case fuchsia::netstack::NetAddressFamily::IPV4:
        if (!net_addr.ipv4) {
          return overnet::Status(overnet::StatusCode::INVALID_ARGUMENT,
                                 "bad ipv4 address");
        }
        udp_addr.ipv4.sin_family = AF_INET;
        udp_addr.ipv4.sin_port = htons(sock_addr.port);
        memcpy(&udp_addr.ipv4.sin_addr, net_addr.ipv4->addr.data(),
               sizeof(udp_addr.ipv4.sin_addr));
        return udp_addr;
      case fuchsia::netstack::NetAddressFamily::IPV6:
        if (!net_addr.ipv6) {
          return overnet::Status(overnet::StatusCode::INVALID_ARGUMENT,
                                 "bad ipv6 address");
        }
        udp_addr.ipv6.sin6_family = AF_INET6;
        udp_addr.ipv6.sin6_port = htons(sock_addr.port);
        memcpy(&udp_addr.ipv6.sin6_addr, net_addr.ipv6->addr.data(),
               sizeof(udp_addr.ipv6.sin6_addr));
        return udp_addr;
    }
    return overnet::Status(overnet::StatusCode::INVALID_ARGUMENT,
                           "bad address family");
  }

  UdpNub* const nub_;
  fuchsia::mdns::MdnsServiceSubscriptionPtr subscription_;
  ServiceMap last_result_;
};

void RunMdnsIntroducer(component::StartupContext* startup_context,
                       UdpNub* nub) {
  fbl::MakeRefCounted<MdnsIntroducer>(nub)->Begin(startup_context);
}

class MdnsAdvertisement::Impl {
 public:
  Impl(component::StartupContext* startup_context, UdpNub* nub)
      : service_(
            startup_context
                ->ConnectToEnvironmentService<fuchsia::mdns::MdnsService>()),
        node_id_(nub->node_id()) {
    std::cerr << "Requesting mDNS advertisement for " << node_id_ << " on port "
              << nub->port() << "\n";
    service_->PublishServiceInstance(
        kServiceName, node_id_.ToString(), nub->port(), {},
        [node_id = node_id_,
         port = nub->port()](fuchsia::mdns::MdnsResult result) {
          std::cout << "Advertising " << node_id << " on port " << port
                    << " via mdns gets: " << result << "\n";
        });
  }
  ~Impl() {
    service_->UnpublishServiceInstance(kServiceName, node_id_.ToString());
  }

 private:
  const fuchsia::mdns::MdnsServicePtr service_;
  const overnet::NodeId node_id_;
};

MdnsAdvertisement::MdnsAdvertisement(component::StartupContext* startup_context,
                                     UdpNub* nub)
    : impl_(std::make_unique<Impl>(startup_context, nub)) {}

MdnsAdvertisement::~MdnsAdvertisement() {}

}  // namespace overnetstack
