// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/mdns_service_impl.h"

#include <fuchsia/device/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <unistd.h>

#include "lib/fidl/cpp/type_converter.h"
#include "src/connectivity/network/mdns/service/mdns_fidl_util.h"
#include "src/connectivity/network/mdns/service/mdns_names.h"
#include "src/lib/fsl/types/type_converters.h"
#include "src/lib/fxl/logging.h"

namespace mdns {
namespace {

static constexpr zx::duration kReadyPollingInterval = zx::sec(1);

std::string GetHostName() {
  char host_name_buffer[HOST_NAME_MAX + 1];
  int result = gethostname(host_name_buffer, sizeof(host_name_buffer));

  std::string host_name;

  if (result < 0) {
    FXL_LOG(ERROR) << "gethostname failed, " << strerror(errno);
    host_name = fuchsia::device::DEFAULT_DEVICE_NAME;
  } else {
    host_name = host_name_buffer;
  }

  return host_name;
}

}  // namespace

MdnsServiceImpl::MdnsServiceImpl(sys::ComponentContext* component_context)
    : component_context_(component_context),
      resolver_bindings_(this, "Resolver"),
      subscriber_bindings_(this, "Subscriber"),
      publisher_bindings_(this, "Publisher") {
  component_context_->outgoing()->AddPublicService<fuchsia::net::mdns::Resolver>(fit::bind_member(
      &resolver_bindings_, &BindingSet<fuchsia::net::mdns::Resolver>::OnBindRequest));
  component_context_->outgoing()->AddPublicService<fuchsia::net::mdns::Subscriber>(fit::bind_member(
      &subscriber_bindings_, &BindingSet<fuchsia::net::mdns::Subscriber>::OnBindRequest));
  component_context_->outgoing()->AddPublicService<fuchsia::net::mdns::Publisher>(fit::bind_member(
      &publisher_bindings_, &BindingSet<fuchsia::net::mdns::Publisher>::OnBindRequest));
  Start();
}

MdnsServiceImpl::~MdnsServiceImpl() {}

void MdnsServiceImpl::Start() {
  std::string host_name = GetHostName();

  if (host_name == fuchsia::device::DEFAULT_DEVICE_NAME) {
    // Host name not set. Try again soon.
    async::PostDelayedTask(
        async_get_default_dispatcher(), [this]() { Start(); }, kReadyPollingInterval);
    return;
  }

  config_.ReadConfigFiles(host_name);
  if (!config_.valid()) {
    FXL_LOG(FATAL) << "Invalid config file(s), terminating: " << config_.error();
    return;
  }

  mdns_.Start(component_context_->svc()->Connect<fuchsia::netstack::Netstack>(), host_name,
              config_.addresses(), config_.perform_host_name_probe(),
              fit::bind_member(this, &MdnsServiceImpl::OnReady));
}

void MdnsServiceImpl::OnReady() {
  ready_ = true;

  // Publish as indicated in config files.
  for (auto& publication : config_.publications()) {
    PublishServiceInstance(publication.service_, publication.instance_,
                           publication.publication_->Clone(), true,
                           [service = publication.service_](
                               fuchsia::net::mdns::Publisher_PublishServiceInstance_Result result) {
                             if (result.is_err()) {
                               FXL_LOG(ERROR) << "Failed to publish as " << service << ", result "
                                              << static_cast<uint32_t>(result.err());
                             }
                           });
  }

  resolver_bindings_.OnReady();
  subscriber_bindings_.OnReady();
  publisher_bindings_.OnReady();
}

bool MdnsServiceImpl::PublishServiceInstance(std::string service_name, std::string instance_name,
                                             std::unique_ptr<Mdns::Publication> publication,
                                             bool perform_probe,
                                             PublishServiceInstanceCallback callback) {
  auto publisher = std::make_unique<SimplePublisher>(std::move(publication), callback.share());

  if (!mdns_.PublishServiceInstance(service_name, instance_name, perform_probe, publisher.get())) {
    return false;
  }

  std::string instance_full_name = MdnsNames::LocalInstanceFullName(instance_name, service_name);

  // |Mdns| told us our instance is unique locally, so the full name should
  // not appear in our collection.
  FXL_DCHECK(publishers_by_instance_full_name_.find(instance_full_name) ==
             publishers_by_instance_full_name_.end());

  publishers_by_instance_full_name_.emplace(instance_full_name, std::move(publisher));

  return true;
}

void MdnsServiceImpl::ResolveHostName(std::string host, int64_t timeout_ns,
                                      ResolveHostNameCallback callback) {
  if (!MdnsNames::IsValidHostName(host)) {
    FXL_LOG(ERROR) << "ResolveHostName called with invalid host name " << host;
    callback(nullptr, nullptr);
    return;
  }

  mdns_.ResolveHostName(
      host, zx::clock::get_monotonic() + zx::nsec(timeout_ns),
      [callback = std::move(callback)](const std::string& host, const inet::IpAddress& v4_address,
                                       const inet::IpAddress& v6_address) {
        callback(v4_address ? std::make_unique<fuchsia::net::Ipv4Address>(
                                  MdnsFidlUtil::CreateIpv4Address(v4_address))
                            : nullptr,
                 v6_address ? std::make_unique<fuchsia::net::Ipv6Address>(
                                  MdnsFidlUtil::CreateIpv6Address(v6_address))
                            : nullptr);
      });
}

void MdnsServiceImpl::SubscribeToService(
    std::string service,
    fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriber> subscriber_handle) {
  if (!MdnsNames::IsValidServiceName(service)) {
    FXL_LOG(ERROR) << "ResolveHostName called with invalid service name " << service;
    return;
  }

  size_t id = next_subscriber_id_++;
  auto subscriber = std::make_unique<Subscriber>(std::move(subscriber_handle),
                                                 [this, id]() { subscribers_by_id_.erase(id); });

  mdns_.SubscribeToService(service, subscriber.get());

  subscribers_by_id_.emplace(id, std::move(subscriber));
}

void MdnsServiceImpl::PublishServiceInstance(
    std::string service, std::string instance, bool perform_probe,
    fidl::InterfaceHandle<fuchsia::net::mdns::PublicationResponder> responder_handle,
    PublishServiceInstanceCallback callback) {
  FXL_DCHECK(responder_handle);

  if (!MdnsNames::IsValidServiceName(service)) {
    FXL_LOG(ERROR) << "PublishServiceInstance called with invalid service name " << service;
    fuchsia::net::mdns::Publisher_PublishServiceInstance_Result result;
    result.set_err(fuchsia::net::mdns::Error::INVALID_SERVICE_NAME);
    callback(std::move(result));
    return;
  }

  if (!MdnsNames::IsValidInstanceName(instance)) {
    FXL_LOG(ERROR) << "PublishServiceInstance called with invalid instance name " << instance;
    fuchsia::net::mdns::Publisher_PublishServiceInstance_Result result;
    result.set_err(fuchsia::net::mdns::Error::INVALID_INSTANCE_NAME);
    callback(std::move(result));
    return;
  }

  auto responder_ptr = responder_handle.Bind();
  FXL_DCHECK(responder_ptr);

  std::string instance_full_name = MdnsNames::LocalInstanceFullName(instance, service);

  auto publisher = std::make_unique<ResponderPublisher>(
      std::move(responder_ptr), std::move(callback), [this, instance_full_name]() {
        publishers_by_instance_full_name_.erase(instance_full_name);
      });

  if (!mdns_.PublishServiceInstance(service, instance, perform_probe, publisher.get())) {
    fuchsia::net::mdns::Publisher_PublishServiceInstance_Result result;
    result.set_err(fuchsia::net::mdns::Error::ALREADY_PUBLISHED_LOCALLY);
    publisher->callback_(std::move(result));
    return;
  }

  // |Mdns| told us our instance is unique locally, so the full name should
  // not appear in our collection.
  FXL_DCHECK(publishers_by_instance_full_name_.find(instance_full_name) ==
             publishers_by_instance_full_name_.end());

  publishers_by_instance_full_name_.emplace(instance_full_name, std::move(publisher));
}

////////////////////////////////////////////////////////////////////////////////
// MdnsServiceImpl::Subscriber implementation

MdnsServiceImpl::Subscriber::Subscriber(
    fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriber> handle, fit::closure deleter) {
  client_.Bind(std::move(handle));
  client_.set_error_handler([this, deleter = std::move(deleter)](zx_status_t status) mutable {
    // Clearing the error handler frees the capture list, so we need to save |deleter|.
    auto save_deleter = std::move(deleter);
    client_.set_error_handler(nullptr);
    client_.Unbind();
    save_deleter();
  });
}

MdnsServiceImpl::Subscriber::~Subscriber() {
  client_.set_error_handler(nullptr);
  if (client_.is_bound()) {
    client_.Unbind();
  }
}

void MdnsServiceImpl::Subscriber::InstanceDiscovered(const std::string& service,
                                                     const std::string& instance,
                                                     const inet::SocketAddress& v4_address,
                                                     const inet::SocketAddress& v6_address,
                                                     const std::vector<std::string>& text,
                                                     uint16_t srv_priority, uint16_t srv_weight) {
  Entry entry{.type = EntryType::kInstanceDiscovered,
              .service_instance = fuchsia::net::mdns::ServiceInstance{.service = service,
                                                                      .instance = instance,
                                                                      .text = text,
                                                                      .srv_priority = srv_priority,
                                                                      .srv_weight = srv_weight}};
  if (v4_address) {
    entry.service_instance.endpoints.push_back(MdnsFidlUtil::CreateEndpointV4(v4_address));
  }

  if (v6_address) {
    entry.service_instance.endpoints.push_back(MdnsFidlUtil::CreateEndpointV6(v6_address));
  }

  entries_.push(std::move(entry));
  MaybeSendNextEntry();
}

void MdnsServiceImpl::Subscriber::InstanceChanged(const std::string& service,
                                                  const std::string& instance,
                                                  const inet::SocketAddress& v4_address,
                                                  const inet::SocketAddress& v6_address,
                                                  const std::vector<std::string>& text,
                                                  uint16_t srv_priority, uint16_t srv_weight) {
  Entry entry{.type = EntryType::kInstanceChanged,
              .service_instance = fuchsia::net::mdns::ServiceInstance{.service = service,
                                                                      .instance = instance,
                                                                      .text = text,
                                                                      .srv_priority = srv_priority,
                                                                      .srv_weight = srv_weight}};
  if (v4_address) {
    entry.service_instance.endpoints.push_back(MdnsFidlUtil::CreateEndpointV4(v4_address));
  }

  if (v6_address) {
    entry.service_instance.endpoints.push_back(MdnsFidlUtil::CreateEndpointV6(v6_address));
  }

  entries_.push(std::move(entry));
  MaybeSendNextEntry();
}

void MdnsServiceImpl::Subscriber::InstanceLost(const std::string& service,
                                               const std::string& instance) {
  entries_.push({.type = EntryType::kInstanceLost,
                 .service_instance = fuchsia::net::mdns::ServiceInstance{.service = service,
                                                                         .instance = instance}});
  MaybeSendNextEntry();
}

void MdnsServiceImpl::Subscriber::MaybeSendNextEntry() {
  FXL_DCHECK(pipeline_depth_ <= kMaxPipelineDepth);
  if (pipeline_depth_ == kMaxPipelineDepth || entries_.empty()) {
    return;
  }

  Entry& entry = entries_.front();
  auto on_reply = fit::bind_member(this, &MdnsServiceImpl::Subscriber::ReplyReceived);

  switch (entry.type) {
    case EntryType::kInstanceDiscovered:
      client_->OnInstanceDiscovered(std::move(entry.service_instance), std::move(on_reply));
      break;
    case EntryType::kInstanceChanged:
      client_->OnInstanceChanged(std::move(entry.service_instance), std::move(on_reply));
      break;
    case EntryType::kInstanceLost:
      client_->OnInstanceLost(entry.service_instance.service, entry.service_instance.instance,
                              std::move(on_reply));
      break;
  }

  ++pipeline_depth_;
  entries_.pop();
}

void MdnsServiceImpl::Subscriber::ReplyReceived() {
  FXL_DCHECK(pipeline_depth_ != 0);
  --pipeline_depth_;
  MaybeSendNextEntry();
}

////////////////////////////////////////////////////////////////////////////////
// MdnsServiceImpl::SimplePublisher implementation

MdnsServiceImpl::SimplePublisher::SimplePublisher(std::unique_ptr<Mdns::Publication> publication,
                                                  PublishServiceInstanceCallback callback)
    : publication_(std::move(publication)), callback_(std::move(callback)) {}

void MdnsServiceImpl::SimplePublisher::ReportSuccess(bool success) {
  fuchsia::net::mdns::Publisher_PublishServiceInstance_Result result;
  if (success) {
    result.set_response(fuchsia::net::mdns::Publisher_PublishServiceInstance_Response());
  } else {
    result.set_err(fuchsia::net::mdns::Error::ALREADY_PUBLISHED_ON_SUBNET);
  }

  callback_(std::move(result));
}

void MdnsServiceImpl::SimplePublisher::GetPublication(
    bool query, const std::string& subtype,
    fit::function<void(std::unique_ptr<Mdns::Publication>)> callback) {
  FXL_DCHECK(subtype.empty() || MdnsNames::IsValidSubtypeName(subtype));
  callback(publication_->Clone());
}

////////////////////////////////////////////////////////////////////////////////
// MdnsServiceImpl::ResponderPublisher implementation

MdnsServiceImpl::ResponderPublisher::ResponderPublisher(
    fuchsia::net::mdns::PublicationResponderPtr responder, PublishServiceInstanceCallback callback,
    fit::closure deleter)
    : responder_(std::move(responder)), callback_(std::move(callback)) {
  FXL_DCHECK(responder_);

  responder_.set_error_handler([this, deleter = std::move(deleter)](zx_status_t status) mutable {
    // Clearing the error handler frees the capture list, so we need to save |deleter|.
    auto save_deleter = std::move(deleter);
    responder_.set_error_handler(nullptr);
    save_deleter();
  });

  responder_.events().SetSubtypes = [this](std::vector<std::string> subtypes) {
    for (auto& subtype : subtypes) {
      if (!MdnsNames::IsValidSubtypeName(subtype)) {
        FXL_LOG(ERROR) << "Invalid subtype " << subtype
                       << " passed in SetSubtypes event, closing connection.";
        responder_ = nullptr;
        Unpublish();
        return;
      }
    }

    SetSubtypes(std::move(subtypes));
  };

  responder_.events().Reannounce = [this]() { Reannounce(); };
}

MdnsServiceImpl::ResponderPublisher::~ResponderPublisher() {
  responder_.set_error_handler(nullptr);
  if (responder_.is_bound()) {
    responder_.Unbind();
  }
}

void MdnsServiceImpl::ResponderPublisher::ReportSuccess(bool success) {
  fuchsia::net::mdns::Publisher_PublishServiceInstance_Result result;
  if (success) {
    result.set_response(fuchsia::net::mdns::Publisher_PublishServiceInstance_Response());
  } else {
    result.set_err(fuchsia::net::mdns::Error::ALREADY_PUBLISHED_ON_SUBNET);
  }

  callback_(std::move(result));
}

void MdnsServiceImpl::ResponderPublisher::GetPublication(
    bool query, const std::string& subtype,
    fit::function<void(std::unique_ptr<Mdns::Publication>)> callback) {
  FXL_DCHECK(subtype.empty() || MdnsNames::IsValidSubtypeName(subtype));
  FXL_DCHECK(responder_);
  responder_->OnPublication(
      query, subtype,
      [this, callback = std::move(callback)](fuchsia::net::mdns::PublicationPtr publication_ptr) {
        if (publication_ptr) {
          for (auto& text : publication_ptr->text) {
            if (!MdnsNames::IsValidTextString(text)) {
              FXL_LOG(ERROR) << "Invalid text string returned by "
                                "Responder.GetPublication, closing connection.";
              responder_ = nullptr;
              Unpublish();
              return;
            }
          }

          if (publication_ptr->ptr_ttl < ZX_SEC(1) || publication_ptr->srv_ttl < ZX_SEC(1) ||
              publication_ptr->txt_ttl < ZX_SEC(1)) {
            FXL_LOG(ERROR) << "TTL less than one second returned by "
                              "Responder.GetPublication, closing connection.";
            responder_ = nullptr;
            Unpublish();
            return;
          }
        }

        callback(MdnsFidlUtil::Convert(publication_ptr));
      });
}

}  // namespace mdns
