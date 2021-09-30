// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <iostream>
#include <string>

#include "garnet/public/lib/fostr/fidl/fuchsia/net/formatting.h"
#include "garnet/public/lib/fostr/fidl/fuchsia/net/mdns/formatting.h"
#include "lib/fidl/cpp/type_converter.h"
#include "src/lib/fsl/types/type_converters.h"

const std::string kLocalArgument = "--local";
const std::string kRemoteArgument = "--remote";
const std::string kServiceName = "_mdnstest._udp.";
const std::string kInstanceName = "mdns_test_instance_name";
const uint16_t kPort = 1234;
const std::vector<std::string> kText = {"chowder", "hammock", "beanstalk"};
constexpr uint16_t kPriority = 4;
constexpr uint16_t kWeight = 5;
const std::string kRemoteHostName = "mdns-test-device-remote";
const fuchsia::net::Ipv4Address kRemoteV4Address{{192, 168, 0, 1}};
const fuchsia::net::Ipv4Address kLocalV4AddressA{{192, 168, 0, 2}};
const fuchsia::net::Ipv4Address kLocalV4AddressB{{192, 168, 0, 3}};
// const fuchsia::net::Ipv6Address kRemoteV6Address{{0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
//                                                  0x46, 0x07, 0x0b, 0xff, 0xfe, 0x60, 0x59,
//                                                  0x5d}};
const zx_duration_t kTimeout = ZX_SEC(60);

namespace mdns::test {

using QuitCallback = fit::function<void(int)>;

////////////////////////////////////////////////////////////////////////////////
// LocalEnd
//
// An instance of this class runs as the 'local' end of the test, which runs
// the tests.
//
// This test verifies that a service published by the remote end is properly
// discovered and that the host name of the remote end can be successfully
// resolved to an IP address.
class LocalEnd : public fuchsia::net::mdns::ServiceSubscriber2 {
 public:
  static std::unique_ptr<LocalEnd> Create(sys::ComponentContext* component_context,
                                          QuitCallback quit_callback) {
    return std::make_unique<LocalEnd>(component_context, std::move(quit_callback));
  }

  LocalEnd(sys::ComponentContext* component_context, QuitCallback quit_callback)
      : component_context_(component_context),
        quit_callback_(std::move(quit_callback)),
        subscriber_binding_(this) {
    subscriber_ = component_context_->svc()->Connect<fuchsia::net::mdns::Subscriber>();

    subscriber_.set_error_handler([this](zx_status_t status) {
      std::cerr << "FAILED: Subscriber channel disconnected unexpectedly, status " << status
                << ".\n";
      Quit(1);
    });

    resolver_ = component_context_->svc()->Connect<fuchsia::net::mdns::Resolver>();

    resolver_.set_error_handler([this](zx_status_t status) {
      std::cerr << "FAILED: Resolver channel disconnected unexpectedly, status " << status << ".\n";
      Quit(1);
    });

    fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriber2> subscriber_handle;

    subscriber_binding_.Bind(subscriber_handle.NewRequest());
    subscriber_binding_.set_error_handler([this](zx_status_t status) {
      std::cerr << "FAILED: Subscriber channel disconnected unexpectedly, status " << status
                << ".\n";
      Quit(1);
    });

    subscriber_->SubscribeToService2(kServiceName, std::move(subscriber_handle));

    resolver_->ResolveHostName(
        kRemoteHostName, kTimeout,
        [this](std::unique_ptr<fuchsia::net::Ipv4Address> v4_address,
               std::unique_ptr<fuchsia::net::Ipv6Address> v6_address) {
          if (!v4_address && !v6_address) {
            std::cerr << "FAILED: Host name resolution timed out.\n";
            Quit(1);
            return;
          }

          if (v4_address) {
            if (!fidl::Equals(*v4_address, kRemoteV4Address)) {
              std::cerr << "FAILED: Host name resolution produced bad V4 address " << *v4_address
                        << "\n";
              Quit(1);
              return;
            }
          } else if (v6_address) {
            // TODO(dalesat): Restore this check once we have predictable addresses in netemul.
            // if (!fidl::Equals(*v6_address, kRemoteV6Address)) {
            //   std::cerr << "FAILED: Host name resolution produced bad V6 address " << *v6_address
            //             << "\n";
            //   Quit(1);
            //   return;
            // }
          } else {
            std::cerr << "FAILED: Host name resolution produced no address\n";
            Quit(1);
            return;
          }

          std::cout << "Host name resolved correctly.\n";
          host_name_resolved_ = true;
          if (instance_discovered_) {
            Quit();
          }
        });
  }

 private:
  // fuchsia::net::mdns::ServiceSubscriber2 implementation.
  void OnInstanceDiscovered(fuchsia::net::mdns::ServiceInstance2 instance,
                            OnInstanceDiscoveredCallback callback) override {
    callback();
    if (VerifyInstance(instance)) {
      std::cout << "Discovered good instance.\n";
      instance_discovered_ = true;
      if (query_count_ == 0) {
        std::cerr << "FAILED: Never got OnQuery.\n";
        Quit(1);
      } else if (host_name_resolved_) {
        Quit();
      }
    } else {
      std::cerr << "FAILED: Discovered instance not compliant." << instance << "\n";
      Quit(1);
    }
  }

  void OnInstanceChanged(fuchsia::net::mdns::ServiceInstance2 instance,
                         OnInstanceChangedCallback callback) override {
    callback();
  }

  void OnInstanceLost(std::string service_name, std::string instance_name,
                      OnInstanceLostCallback callback) override {
    callback();
  }

  void OnQuery(fuchsia::net::mdns::ResourceType resource_type, OnQueryCallback callback) override {
    callback();
    ++query_count_;
  }

  void Quit(int exit_code = 0) {
    FX_DCHECK(quit_callback_);
    subscriber_.set_error_handler(nullptr);
    subscriber_.Unbind();
    subscriber_binding_.set_error_handler(nullptr);
    subscriber_binding_.Unbind();
    resolver_.set_error_handler(nullptr);
    resolver_.Unbind();
    quit_callback_(exit_code);
  }

  bool VerifyInstance(const fuchsia::net::mdns::ServiceInstance2& instance) {
    return instance.service() == kServiceName && instance.instance() == kInstanceName &&
           VerifyRemoteEndpoints(instance) &&
           std::equal(kText.begin(), kText.end(), instance.text().begin(), instance.text().end()) &&
           instance.srv_priority() == kPriority && instance.srv_weight() == kWeight;
  }

  bool VerifyRemoteEndpoints(const fuchsia::net::mdns::ServiceInstance2& instance) {
    bool valid_v4 = false;
    bool valid_v6 = false;
    if (instance.has_ipv4_endpoint()) {
      valid_v4 = instance.ipv4_endpoint().port == kPort &&
                 fidl::Equals(instance.ipv4_endpoint().address, kRemoteV4Address);
    }
    if (instance.has_ipv6_endpoint()) {
      // TODO(dalesat): Restore this check once we have predictable addresses in netemul.
      // fidl::Equals(endpoint.addr.ipv6(), kRemoteV6Address) &&
      valid_v6 = instance.ipv6_endpoint().port == kPort;
    }
    return valid_v4 || valid_v6;
  }

  sys::ComponentContext* component_context_;
  QuitCallback quit_callback_;
  fuchsia::net::mdns::SubscriberPtr subscriber_;
  fidl::Binding<fuchsia::net::mdns::ServiceSubscriber2> subscriber_binding_;
  fuchsia::net::mdns::ResolverPtr resolver_;
  bool instance_discovered_ = false;
  bool host_name_resolved_ = false;
  uint32_t query_count_ = 0;
};

////////////////////////////////////////////////////////////////////////////////
// RemoteEnd
//
// An instance of this class runs as the 'remote' end of the test, responding
// to messages from the local end.
class RemoteEnd : public fuchsia::net::mdns::PublicationResponder2 {
 public:
  static std::unique_ptr<RemoteEnd> Create(sys::ComponentContext* component_context,
                                           QuitCallback quit_callback) {
    return std::make_unique<RemoteEnd>(component_context, std::move(quit_callback));
  }

  RemoteEnd(sys::ComponentContext* component_context, QuitCallback quit_callback)
      : component_context_(component_context),
        quit_callback_(std::move(quit_callback)),
        responder_binding_(this),
        responder_2_binding_(this) {
    publisher_ = component_context_->svc()->Connect<fuchsia::net::mdns::Publisher>();

    publisher_.set_error_handler([this](zx_status_t status) {
      std::cerr << "FAILED: Publisher channel disconnected unexpectedly, status " << status
                << ".\n";
      Quit(1);
    });

    fidl::InterfaceHandle<fuchsia::net::mdns::PublicationResponder2> responder_handle;

    responder_binding_.Bind(responder_handle.NewRequest());
    responder_binding_.set_error_handler([this](zx_status_t status) {
      if (status != ZX_ERR_PEER_CLOSED) {
        std::cerr << "Responder channel disconnected unexpectedly, status " << status << ".\n";
        Quit(1);
        return;
      }

      responder_binding_.set_error_handler(nullptr);
      responder_binding_.Unbind();
    });

    publisher_->PublishServiceInstance2(
        kServiceName, kInstanceName, true, std::move(responder_handle),
        [this](fuchsia::net::mdns::Publisher_PublishServiceInstance2_Result result) {
          if (result.is_response()) {
            std::cout << "Instance successfully published.\n";
          } else {
            std::cerr << "PublishServiceInstance2 failed, err " << result.err() << ".\n";
            Quit(1);
          }
        });

    // Ensure that the service guards against reannouncing when the instance probe hasn't yet
    // completed to avoid regressing b/144188577.
    responder_binding_.events().Reannounce();

    // Publish a second responder for the same service. This ensures that the service can handle
    // the responder being replaced when probing is still underway.
    responder_2_binding_.Bind(responder_handle.NewRequest());
    responder_2_binding_.set_error_handler([this](zx_status_t status) {
      std::cerr << "FAILED: Second responder channel disconnected unexpectedly, status " << status
                << ".\n";
      Quit(1);
    });

    publisher_->PublishServiceInstance2(
        kServiceName, kInstanceName, true, std::move(responder_handle),
        [this](fuchsia::net::mdns::Publisher_PublishServiceInstance2_Result result) {
          if (result.is_response()) {
            std::cout << "Instance successfully republished.\n";
          } else {
            std::cerr << "PublishServiceInstance2 failed, err " << result.err() << ".\n";
            Quit(1);
          }
        });
  }

 private:
  // fuchsia::net::mdns::PublicationResponder2 implementation.
  void OnPublication(bool query, fidl::StringPtr subtype,
                     std::vector<fuchsia::net::IpAddress> source_addresses,
                     OnPublicationCallback callback) override {
    auto publication = std::make_unique<fuchsia::net::mdns::Publication>();
    publication->port = kPort;
    publication->text = kText;
    publication->srv_priority = kPriority;
    publication->srv_weight = kWeight;

    for (auto& source_address : source_addresses) {
      if (source_address.is_ipv4() && source_address.ipv4().addr != kLocalV4AddressA.addr &&
          source_address.ipv4().addr != kLocalV4AddressB.addr) {
        std::cerr << "Unrecognized source address\n";
        Quit(1);
      }
    }

    callback(std::move(publication));
  }

  void Quit(int exit_code = 0) {
    FX_DCHECK(quit_callback_);
    publisher_.set_error_handler(nullptr);
    publisher_.Unbind();
    responder_binding_.set_error_handler(nullptr);
    responder_binding_.Unbind();
    quit_callback_(exit_code);
  }

  sys::ComponentContext* component_context_;
  QuitCallback quit_callback_;
  fuchsia::net::mdns::PublisherPtr publisher_;
  fidl::Binding<fuchsia::net::mdns::PublicationResponder2> responder_binding_;
  fidl::Binding<fuchsia::net::mdns::PublicationResponder2> responder_2_binding_;
};

}  // namespace mdns::test

////////////////////////////////////////////////////////////////////////////////
// main
//
int main(int argc, const char** argv) {
  bool local = false;
  bool remote = false;

  for (int arg_index = 0; arg_index < argc; ++arg_index) {
    if (argv[arg_index] == kLocalArgument) {
      local = true;
    } else if (argv[arg_index] == kRemoteArgument) {
      remote = true;
    }
  }

  if (local == remote) {
    std::cout << "options: " << kLocalArgument << " | " << kRemoteArgument << "\n";
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::unique_ptr<sys::ComponentContext> component_context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();

  int result = 0;

  if (local) {
    auto local_end =
        mdns::test::LocalEnd::Create(component_context.get(), [&loop, &result](int exit_code) {
          result = exit_code;
          async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
        });
    loop.Run();
  } else {
    FX_DCHECK(remote);
    auto remote_end =
        mdns::test::RemoteEnd::Create(component_context.get(), [&loop, &result](int exit_code) {
          result = exit_code;
          async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
        });
    loop.Run();
  }

  return result;
}
