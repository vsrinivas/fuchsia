// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/mdns_service_impl.h"

#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/sys/cpp/component_context.h>
#include <unistd.h>

#include "garnet/bin/mdns/service/mdns_fidl_util.h"
#include "garnet/bin/mdns/service/mdns_names.h"
#include "lib/fidl/cpp/type_converter.h"
#include "lib/fsl/types/type_converters.h"
#include "src/lib/fxl/logging.h"

namespace mdns {
namespace {

static const std::string kPublishAs = "_fuchsia._udp.";
static constexpr uint64_t kPublishPort = 5353;
static const std::string kUnsetHostName = "fuchsia-unset-device-name";
static constexpr zx::duration kReadyPollingInterval = zx::sec(1);

std::string GetHostName() {
  char host_name_buffer[HOST_NAME_MAX + 1];
  int result = gethostname(host_name_buffer, sizeof(host_name_buffer));

  std::string host_name;

  if (result < 0) {
    FXL_LOG(ERROR) << "gethostname failed, " << strerror(errno);
    host_name = kUnsetHostName;
  } else {
    host_name = host_name_buffer;
  }

  return host_name;
}

}  // namespace

MdnsServiceImpl::MdnsServiceImpl(sys::ComponentContext* component_context)
    : component_context_(component_context) {
  component_context_->outgoing()->AddPublicService<fuchsia::mdns::Controller>(
      fit::bind_member(this, &MdnsServiceImpl::OnBindRequest));
  Start();
}

MdnsServiceImpl::~MdnsServiceImpl() {}

void MdnsServiceImpl::Start() {
  std::string host_name = GetHostName();

  if (host_name == kUnsetHostName) {
    // Host name not set. Try again soon.
    async::PostDelayedTask(
        async_get_default_dispatcher(), [this]() { Start(); },
        kReadyPollingInterval);
    return;
  }

  mdns_.Start(component_context_->svc()->Connect<fuchsia::netstack::Netstack>(),
              host_name, fit::bind_member(this, &MdnsServiceImpl::OnReady));
}

void MdnsServiceImpl::OnBindRequest(
    fidl::InterfaceRequest<fuchsia::mdns::Controller> request) {
  if (ready_) {
    bindings_.AddBinding(this, std::move(request));
  } else {
    pending_binding_requests_.push_back(std::move(request));
  }
}

void MdnsServiceImpl::OnReady() {
  ready_ = true;

  // Publish this device as "_fuchsia._udp.".
  // TODO(NET-2188): Make this a config item.
  DEPRECATEDPublishServiceInstance(kPublishAs, mdns_.host_name(), kPublishPort,
                                   fidl::VectorPtr<std::string>(), true,
                                   [this](fuchsia::mdns::Result result) {
                                     if (result != fuchsia::mdns::Result::OK) {
                                       FXL_LOG(ERROR)
                                           << "Failed to publish as "
                                           << kPublishAs << ", result "
                                           << static_cast<uint32_t>(result);
                                     }
                                   });

  for (auto& request : pending_binding_requests_) {
    bindings_.AddBinding(this, std::move(request));
  }

  pending_binding_requests_.clear();
}

void MdnsServiceImpl::ResolveHostName(std::string host_name, int64_t timeout_ns,
                                      ResolveHostNameCallback callback) {
  if (!MdnsNames::IsValidHostName(host_name)) {
    callback(nullptr, nullptr);
    return;
  }

  mdns_.ResolveHostName(
      host_name,
      fxl::TimePoint::Now() + fxl::TimeDelta::FromNanoseconds(timeout_ns),
      [this, callback = std::move(callback)](
          const std::string& host_name, const inet::IpAddress& v4_address,
          const inet::IpAddress& v6_address) {
        callback(MdnsFidlUtil::CreateSocketAddressIPv4(v4_address),
                 MdnsFidlUtil::CreateSocketAddressIPv6(v6_address));
      });
}

void MdnsServiceImpl::SubscribeToService(
    std::string service_name,
    fidl::InterfaceHandle<fuchsia::mdns::ServiceSubscriber> subscriber_handle) {
  if (!MdnsNames::IsValidServiceName(service_name)) {
    return;
  }

  size_t id = next_subscriber_id_++;
  auto subscriber = std::make_unique<Subscriber>(
      std::move(subscriber_handle),
      [this, id]() { subscribers_by_id_.erase(id); });

  mdns_.SubscribeToService(service_name, subscriber.get());

  subscribers_by_id_.emplace(id, std::move(subscriber));
}

void MdnsServiceImpl::DEPRECATEDPublishServiceInstance(
    std::string service_name, std::string instance_name, uint16_t port,
    std::vector<std::string> text, bool perform_probe,
    PublishServiceInstanceCallback callback) {
  if (!MdnsNames::IsValidServiceName(service_name)) {
    callback(fuchsia::mdns::Result::INVALID_SERVICE_NAME);
    return;
  }

  if (!MdnsNames::IsValidInstanceName(instance_name)) {
    callback(fuchsia::mdns::Result::INVALID_INSTANCE_NAME);
    return;
  }

  auto publisher = std::make_unique<SimplePublisher>(
      inet::IpPort::From_uint16_t(port), std::move(text), callback.share());

  if (!mdns_.PublishServiceInstance(service_name, instance_name, perform_probe,
                                    publisher.get())) {
    callback(fuchsia::mdns::Result::ALREADY_PUBLISHED_LOCALLY);
    return;
  }

  MdnsNames::LocalInstanceFullName(instance_name, service_name);

  std::string instance_full_name =
      MdnsNames::LocalInstanceFullName(instance_name, service_name);

  // |Mdns| told us our instance is unique locally, so the full name should
  // not appear in our collection.
  FXL_DCHECK(publishers_by_instance_full_name_.find(instance_full_name) ==
             publishers_by_instance_full_name_.end());

  publishers_by_instance_full_name_.emplace(instance_full_name,
                                            std::move(publisher));
}

void MdnsServiceImpl::DEPRECATEDUnpublishServiceInstance(
    std::string service_name, std::string instance_name) {
  if (!MdnsNames::IsValidServiceName(service_name) ||
      !MdnsNames::IsValidInstanceName(instance_name)) {
    return;
  }

  std::string instance_full_name =
      MdnsNames::LocalInstanceFullName(instance_name, service_name);

  // This will delete the publisher, unpublishing the service instance.
  publishers_by_instance_full_name_.erase(instance_full_name);
}

void MdnsServiceImpl::PublishServiceInstance(
    std::string service_name, std::string instance_name, bool perform_probe,
    fidl::InterfaceHandle<fuchsia::mdns::Responder> responder_handle,
    PublishServiceInstanceCallback callback) {
  FXL_DCHECK(responder_handle);

  auto responder_ptr = responder_handle.Bind();
  FXL_DCHECK(responder_ptr);

  if (!MdnsNames::IsValidServiceName(service_name)) {
    callback(fuchsia::mdns::Result::INVALID_SERVICE_NAME);
    return;
  }

  if (!MdnsNames::IsValidInstanceName(instance_name)) {
    callback(fuchsia::mdns::Result::INVALID_INSTANCE_NAME);
    return;
  }

  std::string instance_full_name =
      MdnsNames::LocalInstanceFullName(instance_name, service_name);

  auto publisher = std::make_unique<ResponderPublisher>(
      std::move(responder_ptr), std::move(callback),
      [this, instance_full_name]() {
        publishers_by_instance_full_name_.erase(instance_full_name);
      });

  if (!mdns_.PublishServiceInstance(service_name, instance_name, perform_probe,
                                    publisher.get())) {
    callback(fuchsia::mdns::Result::ALREADY_PUBLISHED_LOCALLY);
    return;
  }

  // |Mdns| told us our instance is unique locally, so the full name should
  // not appear in our collection.
  FXL_DCHECK(publishers_by_instance_full_name_.find(instance_full_name) ==
             publishers_by_instance_full_name_.end());

  publishers_by_instance_full_name_.emplace(instance_full_name,
                                            std::move(publisher));
}

void MdnsServiceImpl::DEPRECATEDSetVerbose(bool value) {
  mdns_.SetVerbose(value);
}

////////////////////////////////////////////////////////////////////////////////
// MdnsServiceImpl::Subscriber implementation

MdnsServiceImpl::Subscriber::Subscriber(
    fidl::InterfaceHandle<fuchsia::mdns::ServiceSubscriber> handle,
    fit::closure deleter) {
  client_.Bind(std::move(handle));
  client_.set_error_handler(
      [this, deleter = std::move(deleter)](zx_status_t status) {
        client_.set_error_handler(nullptr);
        client_.Unbind();
        deleter();
      });
}

MdnsServiceImpl::Subscriber::~Subscriber() {}

void MdnsServiceImpl::Subscriber::InstanceDiscovered(
    const std::string& service, const std::string& instance,
    const inet::SocketAddress& v4_address,
    const inet::SocketAddress& v6_address,
    const std::vector<std::string>& text) {
  entries_.push(
      {.type = EntryType::kInstanceDiscovered,
       .service_instance = fuchsia::mdns::ServiceInstance{
           .service_name = service,
           .instance_name = instance,
           .v4_address = MdnsFidlUtil::CreateSocketAddressIPv4(v4_address),
           .v6_address = MdnsFidlUtil::CreateSocketAddressIPv6(v6_address),
           .text = fidl::VectorPtr<std::string>(text)}});
  MaybeSendNextEntry();
}

void MdnsServiceImpl::Subscriber::InstanceChanged(
    const std::string& service, const std::string& instance,
    const inet::SocketAddress& v4_address,
    const inet::SocketAddress& v6_address,
    const std::vector<std::string>& text) {
  entries_.push(
      {.type = EntryType::kInstanceChanged,
       .service_instance = fuchsia::mdns::ServiceInstance{
           .service_name = service,
           .instance_name = instance,
           .v4_address = MdnsFidlUtil::CreateSocketAddressIPv4(v4_address),
           .v6_address = MdnsFidlUtil::CreateSocketAddressIPv6(v6_address),
           .text = fidl::VectorPtr<std::string>(text)}});
  MaybeSendNextEntry();
}

void MdnsServiceImpl::Subscriber::InstanceLost(const std::string& service,
                                               const std::string& instance) {
  entries_.push({.type = EntryType::kInstanceLost,
                 .service_instance = fuchsia::mdns::ServiceInstance{
                     .service_name = service, .instance_name = instance}});
  MaybeSendNextEntry();
}

void MdnsServiceImpl::Subscriber::MaybeSendNextEntry() {
  FXL_DCHECK(pipeline_depth_ <= kMaxPipelineDepth);
  if (pipeline_depth_ == kMaxPipelineDepth || entries_.empty()) {
    return;
  }

  Entry& entry = entries_.front();
  auto on_reply =
      fit::bind_member(this, &MdnsServiceImpl::Subscriber::ReplyReceived);

  switch (entry.type) {
    case EntryType::kInstanceDiscovered:
      client_->InstanceDiscovered(std::move(entry.service_instance),
                                  std::move(on_reply));
      break;
    case EntryType::kInstanceChanged:
      client_->InstanceChanged(std::move(entry.service_instance),
                               std::move(on_reply));
      break;
    case EntryType::kInstanceLost:
      client_->InstanceLost(entry.service_instance.service_name,
                            entry.service_instance.instance_name,
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

MdnsServiceImpl::SimplePublisher::SimplePublisher(
    inet::IpPort port, std::vector<std::string> text,
    PublishServiceInstanceCallback callback)
    : port_(port), text_(std::move(text)), callback_(std::move(callback)) {}

void MdnsServiceImpl::SimplePublisher::ReportSuccess(bool success) {
  callback_(success ? fuchsia::mdns::Result::OK
                    : fuchsia::mdns::Result::ALREADY_PUBLISHED_ON_SUBNET);
}

void MdnsServiceImpl::SimplePublisher::GetPublication(
    bool query, const std::string& subtype,
    fit::function<void(std::unique_ptr<Mdns::Publication>)> callback) {
  FXL_DCHECK(subtype.empty() || MdnsNames::IsValidSubtypeName(subtype));
  callback(Mdns::Publication::Create(port_, text_));
}

////////////////////////////////////////////////////////////////////////////////
// MdnsServiceImpl::ResponderPublisher implementation

MdnsServiceImpl::ResponderPublisher::ResponderPublisher(
    fuchsia::mdns::ResponderPtr responder,
    PublishServiceInstanceCallback callback, fit::closure deleter)
    : responder_(std::move(responder)), callback_(std::move(callback)) {
  FXL_DCHECK(responder_);

  responder_.set_error_handler(
      [this, deleter = std::move(deleter)](zx_status_t status) {
        responder_.set_error_handler(nullptr);
        deleter();
      });

  responder_.events().OnSubtypesChanged =
      [this](std::vector<std::string> subtypes) {
        for (auto& subtype : subtypes) {
          if (!MdnsNames::IsValidSubtypeName(subtype)) {
            FXL_LOG(ERROR)
                << "Invalid subtype " << subtype
                << " passed in OnSubtypesChanged event, closing connection.";
            responder_ = nullptr;
            Unpublish();
            return;
          }
        }

        SetSubtypes(std::move(subtypes));
      };

  responder_.events().OnPublicationChanged = [this]() { Reannounce(); };
}

void MdnsServiceImpl::ResponderPublisher::ReportSuccess(bool success) {
  FXL_DCHECK(responder_);
  callback_(success ? fuchsia::mdns::Result::OK
                    : fuchsia::mdns::Result::ALREADY_PUBLISHED_ON_SUBNET);
  callback_ = nullptr;
}

void MdnsServiceImpl::ResponderPublisher::GetPublication(
    bool query, const std::string& subtype,
    fit::function<void(std::unique_ptr<Mdns::Publication>)> callback) {
  FXL_DCHECK(subtype.empty() || MdnsNames::IsValidSubtypeName(subtype));
  FXL_DCHECK(responder_);
  responder_->GetPublication(
      query, subtype,
      [this, callback = std::move(callback)](
          fuchsia::mdns::PublicationPtr publication_ptr) {
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

          if (publication_ptr->ptr_ttl < ZX_SEC(1) ||
              publication_ptr->srv_ttl < ZX_SEC(1) ||
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
