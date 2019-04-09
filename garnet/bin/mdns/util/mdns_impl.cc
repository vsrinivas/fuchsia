// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/util/mdns_impl.h"

#include <fuchsia/mdns/cpp/fidl.h>
#include <lib/async-loop/loop.h>
#include <lib/async/default.h>
#include <poll.h>

#include <iostream>
#include <unordered_set>

#include "garnet/bin/mdns/util/formatting.h"
#include "garnet/bin/mdns/util/mdns_params.h"
#include "lib/fidl/cpp/type_converter.h"
#include "lib/fsl/types/type_converters.h"
#include "src/lib/fxl/logging.h"

namespace mdns {

MdnsImpl::MdnsImpl(sys::ComponentContext* component_context, MdnsParams* params,
                   fit::closure quit_callback)
    : quit_callback_(std::move(quit_callback)),
      responder_binding_(this),
      subscriber_binding_(this) {
  FXL_DCHECK(component_context);
  FXL_DCHECK(params);
  FXL_DCHECK(quit_callback_);

  controller_ = component_context->svc()->Connect<fuchsia::mdns::Controller>();

  controller_.set_error_handler([this](zx_status_t status) {
    controller_.set_error_handler(nullptr);
    controller_.Unbind();
    responder_binding_.Unbind();
    subscriber_binding_.Unbind();
    std::cout << "mDNS service disconnected unexpectedly\n";
    quit_callback_();
  });

  switch (params->command_verb()) {
    case MdnsParams::CommandVerb::kVerbose:
      std::cout << "verbose: logging mDNS traffic\n";
      controller_->DEPRECATEDSetVerbose(true);
      quit_callback_();
      break;
    case MdnsParams::CommandVerb::kQuiet:
      std::cout << "verbose: not logging mDNS traffic\n";
      controller_->DEPRECATEDSetVerbose(false);
      quit_callback_();
      break;
    case MdnsParams::CommandVerb::kResolve:
      Resolve(params->host_name(), params->timeout_seconds());
      break;
    case MdnsParams::CommandVerb::kSubscribe:
      Subscribe(params->service_name());
      break;
    case MdnsParams::CommandVerb::kPublish:
      Publish(params->service_name(), params->instance_name(), params->port(),
              params->text());
      break;
    case MdnsParams::CommandVerb::kUnpublish:
      Unpublish(params->service_name(), params->instance_name());
      break;
    case MdnsParams::CommandVerb::kRespond:
      Respond(params->service_name(), params->instance_name(), params->port(),
              params->announce(), params->text());
      break;
  }
}

MdnsImpl::~MdnsImpl() {}

void MdnsImpl::WaitForKeystroke() {
  fd_waiter_.Wait(
      [this](zx_status_t status, uint32_t events) { HandleKeystroke(); }, 0,
      POLLIN);
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
  controller_->ResolveHostName(
      host_name, timeout_seconds * 1000,
      [this](fuchsia::netstack::SocketAddressPtr v4Address,
             fuchsia::netstack::SocketAddressPtr v6Address) {
        if (v4Address) {
          std::cout << "IPv4 address: " << *v4Address << "\n";
        }

        if (v6Address) {
          std::cout << "IPv6 address: " << *v6Address << "\n";
        }

        if (!v4Address && !v6Address) {
          std::cout << "not found\n";
        }

        controller_.set_error_handler(nullptr);
        controller_.Unbind();
        quit_callback_();
      });
}

void MdnsImpl::Subscribe(const std::string& service_name) {
  std::cout << "subscribing to service " << service_name << "\n";
  std::cout << "press escape key to quit\n";
  fidl::InterfaceHandle<fuchsia::mdns::ServiceSubscriber> subscriber_handle;

  subscriber_binding_.Bind(subscriber_handle.NewRequest());
  subscriber_binding_.set_error_handler([this](zx_status_t status) {
    subscriber_binding_.set_error_handler(nullptr);
    subscriber_binding_.Unbind();
    std::cout << "mDNS service disconnected from subscriber unexpectedly\n";
    quit_callback_();
  });

  controller_->SubscribeToService(service_name, std::move(subscriber_handle));

  WaitForKeystroke();
}

void MdnsImpl::Publish(const std::string& service_name,
                       const std::string& instance_name, uint16_t port,
                       const std::vector<std::string>& text) {
  std::cout << "publishing instance " << instance_name << " of service "
            << service_name << "\n";
  controller_->DEPRECATEDPublishServiceInstance(
      service_name, instance_name, port,
      fidl::To<fidl::VectorPtr<std::string>>(text), true,
      [this](fuchsia::mdns::Result result) {
        UpdateStatus(result);
        quit_callback_();
      });
}

void MdnsImpl::Unpublish(const std::string& service_name,
                         const std::string& instance_name) {
  std::cout << "unpublishing instance " << instance_name << " of service "
            << service_name << "\n";
  controller_->DEPRECATEDUnpublishServiceInstance(service_name, instance_name);
  quit_callback_();
}

void MdnsImpl::Respond(const std::string& service_name,
                       const std::string& instance_name, uint16_t port,
                       const std::vector<std::string>& announce,
                       const std::vector<std::string>& text) {
  std::cout << "responding as instance " << instance_name << " of service "
            << service_name << "\n";
  std::cout << "press escape key to quit\n";
  fidl::InterfaceHandle<fuchsia::mdns::Responder> responder_handle;

  responder_binding_.Bind(responder_handle.NewRequest());
  responder_binding_.set_error_handler([this](zx_status_t status) {
    responder_binding_.set_error_handler(nullptr);
    responder_binding_.Unbind();
    std::cout << "mDNS service disconnected from responder unexpectedly\n";
    quit_callback_();
  });

  publication_port_ = port;
  publication_text_ = text;

  controller_->PublishServiceInstance(
      service_name, instance_name, true, std::move(responder_handle),
      [this](fuchsia::mdns::Result result) { UpdateStatus(result); });

  if (!announce.empty()) {
    responder_binding_.events().OnSubtypesChanged(announce);
  }

  WaitForKeystroke();
}

void MdnsImpl::UpdateStatus(fuchsia::mdns::Result result) {
  switch (result) {
    case fuchsia::mdns::Result::OK:
      std::cout << "instance successfully published\n";
      return;
    case fuchsia::mdns::Result::INVALID_SERVICE_NAME:
      std::cout << "ERROR: service name is invalid\n";
      break;
    case fuchsia::mdns::Result::INVALID_INSTANCE_NAME:
      std::cout << "ERROR: instance name is invalid\n";
      break;
    case fuchsia::mdns::Result::ALREADY_PUBLISHED_LOCALLY:
      std::cout << "ERROR: instance was already published by this host\n";
      break;
    case fuchsia::mdns::Result::ALREADY_PUBLISHED_ON_SUBNET:
      std::cout << "ERROR: instance was already published by another "
                   "host on the subnet\n";
      break;
      // The default case has been deliberately omitted here so that this switch
      // statement will be updated whenever the |Result| enum is changed.
  }

  quit_callback_();
}

void MdnsImpl::GetPublication(bool query, fidl::StringPtr subtype,
                              GetPublicationCallback callback) {
  std::cout << (query ? "query" : "initial publication");
  if (subtype) {
    std::cout << " for subtype " << subtype;
  }

  std::cout << "\n";

  auto publication = fuchsia::mdns::Publication::New();
  publication->port = publication_port_;
  publication->text = fidl::To<fidl::VectorPtr<std::string>>(publication_text_);

  callback(std::move(publication));
}

void MdnsImpl::InstanceDiscovered(fuchsia::mdns::ServiceInstance instance,
                                  InstanceDiscoveredCallback callback) {
  std::cout << "discovered:" << fostr::Indent << fostr::NewLine << instance
            << fostr::Outdent << "\n";
  callback();
}

void MdnsImpl::InstanceChanged(fuchsia::mdns::ServiceInstance instance,
                               InstanceChangedCallback callback) {
  std::cout << "changed:" << fostr::Indent << fostr::NewLine << instance
            << fostr::Outdent << "\n";
  callback();
}

void MdnsImpl::InstanceLost(std::string service_name, std::string instance_name,
                            InstanceLostCallback callback) {
  std::cout << "lost:" << fostr::Indent << fostr::NewLine << service_name << " "
            << instance_name << fostr::Outdent << "\n";
  callback();
}

}  // namespace mdns
