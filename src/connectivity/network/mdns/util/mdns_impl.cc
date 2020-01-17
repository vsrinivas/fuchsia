// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/util/mdns_impl.h"

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/default.h>
#include <poll.h>

#include <iostream>
#include <unordered_set>

#include "lib/fidl/cpp/type_converter.h"
#include "src/connectivity/network/mdns/util/formatting.h"
#include "src/connectivity/network/mdns/util/mdns_params.h"
#include "src/lib/fsl/types/type_converters.h"
#include "src/lib/syslog/cpp/logger.h"

namespace mdns {

MdnsImpl::MdnsImpl(sys::ComponentContext* component_context, MdnsParams* params,
                   fit::closure quit_callback)
    : component_context_(component_context),
      quit_callback_(std::move(quit_callback)),
      responder_binding_(this),
      subscriber_binding_(this) {
  FX_DCHECK(component_context);
  FX_DCHECK(params);
  FX_DCHECK(quit_callback_);

  switch (params->command_verb()) {
    case MdnsParams::CommandVerb::kResolve:
      Resolve(params->host_name(), params->timeout_seconds());
      break;
    case MdnsParams::CommandVerb::kSubscribe:
      Subscribe(params->service_name());
      break;
    case MdnsParams::CommandVerb::kRespond:
      Respond(params->service_name(), params->instance_name(), params->port(), params->announce(),
              params->text());
      break;
  }
}

MdnsImpl::~MdnsImpl() {}

void MdnsImpl::WaitForKeystroke() {
  fd_waiter_.Wait([this](zx_status_t status, uint32_t events) { HandleKeystroke(); }, 0, POLLIN);
}

void MdnsImpl::HandleKeystroke() {
  int c = getc(stdin);

  if (c == 27) {
    quit_callback_();
  }

  WaitForKeystroke();
}

void MdnsImpl::Resolve(const std::string& host_name, uint32_t timeout_seconds) {
  std::cout << "resolving " << host_name << "\n";
  EnsureResolver();
  resolver_->ResolveHostName(
      host_name, timeout_seconds * 1000,
      [this](fuchsia::net::Ipv4AddressPtr v4Address, fuchsia::net::Ipv6AddressPtr v6Address) {
        if (v4Address) {
          std::cout << "IPv4 address: " << *v4Address << "\n";
        }

        if (v6Address) {
          std::cout << "IPv6 address: " << *v6Address << "\n";
        }

        if (!v4Address && !v6Address) {
          std::cout << "not found\n";
        }

        Quit();
      });
}

void MdnsImpl::Subscribe(const std::string& service_name) {
  std::cout << "subscribing to service " << service_name << "\n";
  std::cout << "press escape key to quit\n";
  fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriber> subscriber_handle;

  subscriber_binding_.Bind(subscriber_handle.NewRequest());
  subscriber_binding_.set_error_handler([this](zx_status_t status) {
    std::cout << "mDNS service disconnected from subscriber unexpectedly\n";
    Quit();
  });

  EnsureSubscriber();
  subscriber_->SubscribeToService(service_name, std::move(subscriber_handle));

  WaitForKeystroke();
}

void MdnsImpl::Respond(const std::string& service_name, const std::string& instance_name,
                       uint16_t port, const std::vector<std::string>& announce,
                       const std::vector<std::string>& text) {
  std::cout << "responding as instance " << instance_name << " of service " << service_name << "\n";
  std::cout << "press escape key to quit\n";
  fidl::InterfaceHandle<fuchsia::net::mdns::PublicationResponder> responder_handle;

  responder_binding_.Bind(responder_handle.NewRequest());
  responder_binding_.set_error_handler([this](zx_status_t status) {
    std::cout << "mDNS service disconnected from responder unexpectedly\n";
    Quit();
  });

  publication_port_ = port;
  publication_text_ = text;

  EnsurePublisher();
  publisher_->PublishServiceInstance(
      service_name, instance_name, true, std::move(responder_handle),
      [this](fuchsia::net::mdns::Publisher_PublishServiceInstance_Result result) {
        if (result.is_response()) {
          std::cout << "instance successfully published\n";
        } else {
          switch (result.err()) {
            case fuchsia::net::mdns::Error::INVALID_SERVICE_NAME:
              std::cout << "ERROR: service name is invalid\n";
              break;
            case fuchsia::net::mdns::Error::INVALID_INSTANCE_NAME:
              std::cout << "ERROR: instance name is invalid\n";
              break;
            case fuchsia::net::mdns::Error::ALREADY_PUBLISHED_LOCALLY:
              std::cout << "ERROR: instance was already published by this host\n";
              break;
            case fuchsia::net::mdns::Error::ALREADY_PUBLISHED_ON_SUBNET:
              std::cout << "ERROR: instance was already published by another "
                           "host on the subnet\n";
              break;
              // The default case has been deliberately omitted here so that
              // this switch statement will be updated whenever the |Result|
              // enum is changed.
          }

          Quit();
        }
      });

  if (!announce.empty()) {
    responder_binding_.events().SetSubtypes(announce);
  }

  WaitForKeystroke();
}

void MdnsImpl::EnsureResolver() {
  if (resolver_) {
    return;
  }

  resolver_ = component_context_->svc()->Connect<fuchsia::net::mdns::Resolver>();

  resolver_.set_error_handler([this](zx_status_t status) {
    std::cout << "fuchsia::net::mdns::Resolver channel disconnected unexpectedly\n";
    Quit();
  });
}

void MdnsImpl::EnsureSubscriber() {
  if (subscriber_) {
    return;
  }

  subscriber_ = component_context_->svc()->Connect<fuchsia::net::mdns::Subscriber>();

  subscriber_.set_error_handler([this](zx_status_t status) {
    std::cout << "fuchsia::net::mdns::Subscriber channel disconnected unexpectedly\n";
    Quit();
  });
}

void MdnsImpl::EnsurePublisher() {
  if (publisher_) {
    return;
  }

  publisher_ = component_context_->svc()->Connect<fuchsia::net::mdns::Publisher>();

  publisher_.set_error_handler([this](zx_status_t status) {
    std::cout << "fuchsia::net::mdns::Publisher channel disconnected unexpectedly\n";
    Quit();
  });
}

void MdnsImpl::Quit() {
  resolver_.set_error_handler(nullptr);
  publisher_.set_error_handler(nullptr);
  subscriber_.set_error_handler(nullptr);
  responder_binding_.set_error_handler(nullptr);
  subscriber_binding_.set_error_handler(nullptr);

  resolver_.Unbind();
  publisher_.Unbind();
  subscriber_.Unbind();
  responder_binding_.Unbind();
  subscriber_binding_.Unbind();

  quit_callback_();
}

void MdnsImpl::OnPublication(bool query, fidl::StringPtr subtype, OnPublicationCallback callback) {
  std::cout << (query ? "query" : "initial publication");
  if (subtype) {
    std::cout << " for subtype " << subtype;
  }

  std::cout << "\n";

  auto publication = fuchsia::net::mdns::Publication::New();
  publication->port = publication_port_;
  publication->text = publication_text_;

  callback(std::move(publication));
}

void MdnsImpl::OnInstanceDiscovered(fuchsia::net::mdns::ServiceInstance instance,
                                    OnInstanceDiscoveredCallback callback) {
  std::cout << "discovered:" << fostr::Indent << fostr::NewLine << instance << fostr::Outdent
            << "\n";
  callback();
}

void MdnsImpl::OnInstanceChanged(fuchsia::net::mdns::ServiceInstance instance,
                                 OnInstanceChangedCallback callback) {
  std::cout << "changed:" << fostr::Indent << fostr::NewLine << instance << fostr::Outdent << "\n";
  callback();
}

void MdnsImpl::OnInstanceLost(std::string service_name, std::string instance_name,
                              OnInstanceLostCallback callback) {
  std::cout << "lost:" << fostr::Indent << fostr::NewLine << service_name << " " << instance_name
            << fostr::Outdent << "\n";
  callback();
}

}  // namespace mdns
