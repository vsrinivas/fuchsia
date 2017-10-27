// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/mdns_impl.h"

#include <iostream>
#include <unordered_set>

#include "garnet/bin/mdns/formatting.h"
#include "garnet/bin/mdns/mdns_params.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/netconnector/fidl/mdns.fidl.h"

namespace mdns {
namespace {

template <typename T>
bool operator==(const fidl::Array<T>& array_a, const fidl::Array<T>& array_b) {
  if (array_a.size() != array_b.size()) {
    return false;
  }

  auto iter_a = array_a.begin();
  for (auto& item_b : array_b) {
    FXL_DCHECK(iter_a != array_a.end());
    if (*iter_a != item_b) {
      return false;
    }

    ++iter_a;
  }

  return true;
}

template <typename T>
bool operator!=(const fidl::Array<T>& array_a, const fidl::Array<T>& array_b) {
  return !(array_a == array_b);
}

bool operator==(const netstack::NetAddressPtr& addr_a,
                const netstack::NetAddressPtr& addr_b) {
  return (addr_a.get() == addr_b.get()) ||
         (addr_a && addr_a->family == addr_b->family &&
          addr_a->ipv4 == addr_b->ipv4 && addr_a->ipv6 == addr_b->ipv6);
}

bool operator==(const netstack::SocketAddressPtr& addr_a,
                const netstack::SocketAddressPtr& addr_b) {
  return (addr_a.get() == addr_b.get()) ||
         (addr_a && addr_a->port == addr_b->port &&
          addr_a->addr == addr_b->addr);
}

bool operator!=(const netstack::SocketAddressPtr& addr_a,
                const netstack::SocketAddressPtr& addr_b) {
  return !(addr_a == addr_b);
}

// Prints the differences between |new_array| and |old_array| to |std::cout|.
void ShowDiff(
    const fidl::Array<netconnector::MdnsServiceInstancePtr>& new_array,
    const fidl::Array<netconnector::MdnsServiceInstancePtr>& old_array) {
  for (auto& new_instance : new_array) {
    bool found = false;

    for (auto& old_instance : old_array) {
      if (new_instance->service_name == old_instance->service_name &&
          new_instance->instance_name == old_instance->instance_name) {
        if (new_instance->v4_address != old_instance->v4_address ||
            new_instance->v6_address != old_instance->v6_address ||
            new_instance->text != old_instance->text) {
          std::cout << "changed:\n"
                    << indent << begl << *new_instance << outdent << "\n";
        }

        found = true;
        break;
      }
    }

    if (!found) {
      std::cout << "added:\n"
                << indent << begl << *new_instance << outdent << "\n";
    }
  }

  for (auto& old_instance : old_array) {
    bool found = false;

    for (auto& new_instance : new_array) {
      if (new_instance->service_name == old_instance->service_name &&
          new_instance->instance_name == old_instance->instance_name) {
        found = true;
        break;
      }
    }

    if (!found) {
      std::cout << "removed:\n"
                << indent << begl << *old_instance << outdent << "\n";
    }
  }
}

}  // namespace

MdnsImpl::MdnsImpl(app::ApplicationContext* application_context,
                   MdnsParams* params)
    : binding_(this) {
  FXL_DCHECK(application_context);
  FXL_DCHECK(params);

  mdns_service_ =
      application_context
          ->ConnectToEnvironmentService<netconnector::MdnsService>();

  mdns_service_.set_connection_error_handler([this]() {
    mdns_service_.set_connection_error_handler(nullptr);
    mdns_service_.reset();
    std::cout << "mDNS service disconnected unexpectedly\n";
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  });

  switch (params->command_verb()) {
    case MdnsParams::CommandVerb::kVerbose:
      std::cout << "verbose: logging mDNS traffic\n";
      mdns_service_->SetVerbose(true);
      fsl::MessageLoop::GetCurrent()->PostQuitTask();
      break;
    case MdnsParams::CommandVerb::kQuiet:
      std::cout << "verbose: not logging mDNS traffic\n";
      mdns_service_->SetVerbose(false);
      fsl::MessageLoop::GetCurrent()->PostQuitTask();
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
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  }

  WaitForKeystroke();
}

void MdnsImpl::Resolve(const std::string& host_name, uint32_t timeout_seconds) {
  std::cout << "resolving " << host_name << "\n";
  mdns_service_->ResolveHostName(
      host_name, timeout_seconds * 1000,
      [this](netstack::SocketAddressPtr v4Address,
             netstack::SocketAddressPtr v6Address) {
        if (v4Address) {
          std::cout << "IPv4 address: " << *v4Address << "\n";
        }

        if (v6Address) {
          std::cout << "IPv6 address: " << *v6Address << "\n";
        }

        if (!v4Address && !v6Address) {
          std::cout << "not found\n";
        }

        mdns_service_.set_connection_error_handler(nullptr);
        mdns_service_.reset();
        fsl::MessageLoop::GetCurrent()->PostQuitTask();
      });
}

void MdnsImpl::Subscribe(const std::string& service_name) {
  std::cout << "subscribing to service " << service_name << "\n";
  std::cout << "press escape key to quit\n";
  netconnector::MdnsServiceSubscriptionPtr subscription;
  mdns_service_->SubscribeToService(service_name, subscription_.NewRequest());
  HandleSubscriptionInstances();
  WaitForKeystroke();
}

void MdnsImpl::Publish(const std::string& service_name,
                       const std::string& instance_name,
                       uint16_t port,
                       const std::vector<std::string>& text) {
  std::cout << "publishing instance " << instance_name << " of service "
            << service_name << "\n";
  mdns_service_->PublishServiceInstance(
      service_name, instance_name, port, fidl::Array<fidl::String>::From(text),
      [this](netconnector::MdnsResult result) {
        UpdateStatus(result);
        fsl::MessageLoop::GetCurrent()->PostQuitTask();
      });
}

void MdnsImpl::Unpublish(const std::string& service_name,
                         const std::string& instance_name) {
  std::cout << "unpublishing instance " << instance_name << " of service "
            << service_name << "\n";
  mdns_service_->UnpublishServiceInstance(service_name, instance_name);
  fsl::MessageLoop::GetCurrent()->PostQuitTask();
}

void MdnsImpl::Respond(const std::string& service_name,
                       const std::string& instance_name,
                       uint16_t port,
                       const std::vector<std::string>& announce,
                       const std::vector<std::string>& text) {
  std::cout << "responding as instance " << instance_name << " of service "
            << service_name << "\n";
  std::cout << "press escape key to quit\n";
  fidl::InterfaceHandle<netconnector::MdnsResponder> responder_handle;

  binding_.Bind(&responder_handle);
  binding_.set_connection_error_handler([this]() {
    binding_.set_connection_error_handler(nullptr);
    binding_.Close();
    std::cout << "mDNS service disconnected from responder unexpectedly\n";
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  });

  publication_port_ = port;
  publication_text_ = text;

  mdns_service_->AddResponder(service_name, instance_name,
                              std::move(responder_handle));

  if (!announce.empty()) {
    mdns_service_->SetSubtypes(service_name, instance_name,
                               fidl::Array<fidl::String>::From(announce));
  }

  WaitForKeystroke();
}

void MdnsImpl::HandleSubscriptionInstances(
    uint64_t version,
    fidl::Array<netconnector::MdnsServiceInstancePtr> instances) {
  if (instances) {
    ShowDiff(instances, prev_instances_);
    prev_instances_ = std::move(instances);
  }

  subscription_->GetInstances(
      version,
      [this](uint64_t version,
             fidl::Array<netconnector::MdnsServiceInstancePtr> instances) {
        HandleSubscriptionInstances(version, std::move(instances));
      });
}

void MdnsImpl::UpdateStatus(netconnector::MdnsResult result) {
  switch (result) {
    case netconnector::MdnsResult::OK:
      std::cout << "instance successfully published\n";
      return;
    case netconnector::MdnsResult::INVALID_SERVICE_NAME:
      std::cout << "ERROR: service name is invalid\n";
      break;
    case netconnector::MdnsResult::INVALID_INSTANCE_NAME:
      std::cout << "ERROR: instance name is invalid\n";
      break;
    case netconnector::MdnsResult::ALREADY_PUBLISHED_LOCALLY:
      std::cout << "ERROR: instance was already published by this host\n";
      break;
    case netconnector::MdnsResult::ALREADY_PUBLISHED_ON_SUBNET:
      std::cout << "ERROR: instance was already published by another "
                   "host on the subnet\n";
      break;
      // The default case has been deliberately omitted here so that this switch
      // statement will be updated whenever the |MdnsResult| enum is changed.
  }

  fsl::MessageLoop::GetCurrent()->PostQuitTask();
}

void MdnsImpl::GetPublication(bool query,
                              const fidl::String& subtype,
                              const GetPublicationCallback& callback) {
  std::cout << (query ? "query" : "initial publication");
  if (subtype) {
    std::cout << " for subtype " << subtype;
  }

  std::cout << "\n";

  auto publication = netconnector::MdnsPublication::New();
  publication->port = publication_port_;
  publication->text = fidl::Array<fidl::String>::From(publication_text_);

  callback(std::move(publication));
}

}  // namespace mdns
