// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/component_context.h>

#include <algorithm>
#include <iostream>
#include <string>

#include "garnet/public/lib/fostr/fidl/fuchsia/net/formatting.h"
#include "garnet/public/lib/fostr/fidl/fuchsia/net/mdns/formatting.h"
#include "lib/fidl/cpp/type_converter.h"
#include "src/lib/fsl/types/type_converters.h"
#include "src/lib/fxl/logging.h"

const std::string kLocalArgument = "--local";
const std::string kRemoteArgument = "--remote";
const std::string kServiceName = "_mdnstest._udp.";
const std::string kTransientServiceName = "_mdnstest2._udp.";
const std::string kInstanceName = "mdns_test_instance_name";
const uint16_t kPort = 1234;
const std::vector<std::string> kText = {"chowder", "hammock", "beanstalk"};
constexpr uint16_t kPriority = 4;
constexpr uint16_t kWeight = 5;
const std::string kRemoteHostName = "mdns-test-device-remote";
const fuchsia::net::Ipv4Address kRemoteAddress{{192, 168, 0, 1}};
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
class LocalEnd : public fuchsia::net::mdns::ServiceSubscriber {
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

    fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriber> subscriber_handle;

    subscriber_binding_.Bind(subscriber_handle.NewRequest());
    subscriber_binding_.set_error_handler([this](zx_status_t status) {
      std::cerr << "FAILED: Subscriber channel disconnected unexpectedly, status " << status
                << ".\n";
      Quit(1);
    });

    subscriber_->SubscribeToService(kServiceName, std::move(subscriber_handle));

    resolver_->ResolveHostName(
        kRemoteHostName, kTimeout,
        [this](std::unique_ptr<fuchsia::net::Ipv4Address> v4_address,
               std::unique_ptr<fuchsia::net::Ipv6Address> v6_address) {
          if (!v4_address && !v6_address) {
            std::cerr << "FAILED: Host name resolution timed out.\n";
            Quit(1);
            return;
          }

          if (!v4_address) {
            std::cerr << "FAILED: Host name resolution didn't product V4 address.\n";
            Quit(1);
            return;
          }

          if (!fidl::Equals(*v4_address, kRemoteAddress)) {
            std::cerr << "FAILED: Host name resolution produced bad V4 address " << *v4_address
                      << "\n";
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
  // fuchsia::net::mdns::ServiceSubscriber implementation.
  void OnInstanceDiscovered(fuchsia::net::mdns::ServiceInstance instance,
                            OnInstanceDiscoveredCallback callback) override {
    callback();
    if (VerifyInstance(instance)) {
      std::cout << "Discovered good instance.\n";
      instance_discovered_ = true;
      if (host_name_resolved_) {
        Quit();
      }
    } else {
      std::cerr << "FAILED: Discovered instance not compliant." << instance << "\n";
      Quit(1);
    }
  }

  void OnInstanceChanged(fuchsia::net::mdns::ServiceInstance instance,
                         OnInstanceChangedCallback callback) override {
    callback();
  }

  void OnInstanceLost(std::string service_name, std::string instance_name,
                      OnInstanceLostCallback callback) override {
    callback();
  }

  void Quit(int exit_code = 0) {
    FXL_DCHECK(quit_callback_);
    subscriber_.set_error_handler(nullptr);
    subscriber_.Unbind();
    subscriber_binding_.set_error_handler(nullptr);
    subscriber_binding_.Unbind();
    resolver_.set_error_handler(nullptr);
    resolver_.Unbind();
    quit_callback_(exit_code);
  }

  bool VerifyInstance(const fuchsia::net::mdns::ServiceInstance& instance) {
    return instance.service == kServiceName && instance.instance == kInstanceName &&
           !instance.endpoints.empty() &&
           (VerifyRemoteEndpoint(instance.endpoints[0]) ||
            (instance.endpoints.size() == 2 && VerifyRemoteEndpoint(instance.endpoints[1]))) &&
           std::equal(kText.begin(), kText.end(), instance.text.begin(), instance.text.end()) &&
           instance.srv_priority == kPriority && instance.srv_weight == kWeight;
  }

  bool VerifyRemoteEndpoint(const fuchsia::net::Endpoint& endpoint) {
    return endpoint.port == kPort && endpoint.addr.is_ipv4() &&
           fidl::Equals(endpoint.addr.ipv4(), kRemoteAddress);
  }

  sys::ComponentContext* component_context_;
  QuitCallback quit_callback_;
  fuchsia::net::mdns::SubscriberPtr subscriber_;
  fidl::Binding<fuchsia::net::mdns::ServiceSubscriber> subscriber_binding_;
  fuchsia::net::mdns::ResolverPtr resolver_;
  bool instance_discovered_ = false;
  bool host_name_resolved_ = false;
};

////////////////////////////////////////////////////////////////////////////////
// RemoteEnd
//
// An instance of this class runs as the 'remote' end of the test, responding
// to messages from the local end.
class RemoteEnd : public fuchsia::net::mdns::PublicationResponder {
 public:
  static std::unique_ptr<RemoteEnd> Create(sys::ComponentContext* component_context,
                                           QuitCallback quit_callback) {
    return std::make_unique<RemoteEnd>(component_context, std::move(quit_callback));
  }

  RemoteEnd(sys::ComponentContext* component_context, QuitCallback quit_callback)
      : component_context_(component_context),
        quit_callback_(std::move(quit_callback)),
        responder_binding_(this) {
    publisher_ = component_context_->svc()->Connect<fuchsia::net::mdns::Publisher>();

    publisher_.set_error_handler([this](zx_status_t status) {
      std::cerr << "FAILED: Publisher channel disconnected unexpectedly, status " << status
                << ".\n";
      Quit(1);
    });

    fidl::InterfaceHandle<fuchsia::net::mdns::PublicationResponder> responder_handle;

    responder_binding_.Bind(responder_handle.NewRequest());
    responder_binding_.set_error_handler([this](zx_status_t status) {
      std::cerr << "Responder channel disconnected unexpectedly, status " << status << ".\n";
      Quit(1);
    });

    publisher_->PublishServiceInstance(
        kServiceName, kInstanceName, true, std::move(responder_handle),
        [this](fuchsia::net::mdns::Publisher_PublishServiceInstance_Result result) {
          if (result.is_response()) {
            std::cout << "Instance successfully published.\n";
          } else {
            std::cerr << "PublishServiceInstance failed, err " << result.err() << ".\n";
            Quit(1);
          }
        });

    // Ensure that the service guards against reannouncing when the instance probe hasn't yet
    // completed to avoid regressing b/144188577.
    responder_binding_.events().Reannounce();

    // Publish a second responder and drop the responder handle immediately. This ensures that the
    // service can handle the responder going away when probing is still underway.
    fidl::Binding<fuchsia::net::mdns::PublicationResponder> transient_responder_binding(this);
    transient_responder_binding.Bind(responder_handle.NewRequest());
    transient_responder_binding.set_error_handler([this](zx_status_t status) {
      std::cerr << "Transient responder channel disconnected unexpectedly, status " << status
                << ".\n";
      Quit(1);
    });

    publisher_->PublishServiceInstance(
        kTransientServiceName, kInstanceName, true, std::move(responder_handle),
        [this](fuchsia::net::mdns::Publisher_PublishServiceInstance_Result result) {
          if (result.is_response()) {
            std::cout << "Transient instance successfully published.\n";
          } else {
            std::cerr << "PublishServiceInstance failed, err " << result.err() << ".\n";
            Quit(1);
          }
        });

    transient_responder_binding.Unbind();
  }

 private:
  // fuchsia::net::mdns::PublicationResponder implementation.
  void OnPublication(bool query, fidl::StringPtr subtype, OnPublicationCallback callback) override {
    auto publication = fuchsia::net::mdns::Publication::New();
    publication->port = kPort;
    publication->text = kText;
    publication->srv_priority = kPriority;
    publication->srv_weight = kWeight;

    callback(std::move(publication));
  }

  void Quit(int exit_code = 0) {
    FXL_DCHECK(quit_callback_);
    publisher_.set_error_handler(nullptr);
    publisher_.Unbind();
    responder_binding_.set_error_handler(nullptr);
    responder_binding_.Unbind();
    quit_callback_(exit_code);
  }

  sys::ComponentContext* component_context_;
  QuitCallback quit_callback_;
  fuchsia::net::mdns::PublisherPtr publisher_;
  fidl::Binding<fuchsia::net::mdns::PublicationResponder> responder_binding_;
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

  std::unique_ptr<sys::ComponentContext> component_context = sys::ComponentContext::Create();

  int result = 0;

  if (local) {
    auto local_end =
        mdns::test::LocalEnd::Create(component_context.get(), [&loop, &result](int exit_code) {
          result = exit_code;
          async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
        });
    loop.Run();
  } else {
    FXL_DCHECK(remote);
    auto remote_end =
        mdns::test::RemoteEnd::Create(component_context.get(), [&loop, &result](int exit_code) {
          result = exit_code;
          async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
        });
    loop.Run();
  }

  return result;
}
