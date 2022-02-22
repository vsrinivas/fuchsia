// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/services/mdns_service_impl.h"

#include <fuchsia/device/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <unistd.h>

#include "lib/fidl/cpp/type_converter.h"
#include "src/connectivity/network/mdns/service/common/mdns_fidl_util.h"
#include "src/connectivity/network/mdns/service/common/mdns_names.h"
#include "src/lib/fsl/types/type_converters.h"

namespace fidl {

template <>
struct TypeConverter<mdns::Media, fuchsia::net::mdns::Media> {
  static mdns::Media Convert(fuchsia::net::mdns::Media value) {
    switch (value) {
      case fuchsia::net::mdns::Media::WIRED:
        return mdns::Media::kWired;
      case fuchsia::net::mdns::Media::WIRELESS:
        return mdns::Media::kWireless;
      default:
        FX_DCHECK(value ==
                  (fuchsia::net::mdns::Media::WIRED | fuchsia::net::mdns::Media::WIRELESS));
        return mdns::Media::kBoth;
    }
  }
};

template <>
struct TypeConverter<fuchsia::net::mdns::PublicationCause, mdns::Mdns::PublicationCause> {
  static fuchsia::net::mdns::PublicationCause Convert(mdns::Mdns::PublicationCause value) {
    switch (value) {
      case mdns::Mdns::PublicationCause::kAnnouncement:
        return fuchsia::net::mdns::PublicationCause::ANNOUNCEMENT;
      case mdns::Mdns::PublicationCause::kQueryMulticastResponse:
        return fuchsia::net::mdns::PublicationCause::QUERY_MULTICAST_RESPONSE;
      case mdns::Mdns::PublicationCause::kQueryUnicastResponse:
        return fuchsia::net::mdns::PublicationCause::QUERY_UNICAST_RESPONSE;
    }
  }
};

}  // namespace fidl

namespace mdns {
namespace {

static constexpr zx::duration kReadyPollingInterval = zx::sec(1);

std::string GetHostName() {
  char host_name_buffer[HOST_NAME_MAX + 1];
  int result = gethostname(host_name_buffer, sizeof(host_name_buffer));

  std::string host_name;

  if (result < 0) {
    FX_LOGS(ERROR) << "gethostname failed, " << strerror(errno);
    host_name = fuchsia::device::DEFAULT_DEVICE_NAME;
  } else {
    host_name = host_name_buffer;
  }

  return host_name;
}

}  // namespace

MdnsServiceImpl::MdnsServiceImpl(sys::ComponentContext* component_context)
    : component_context_(component_context),
      service_instance_resolver_bindings_(this, "ServiceInstanceResolver"),
      mdns_(transceiver_),
      deprecated_services_(mdns_, component_context) {
  component_context_->outgoing()->AddPublicService<fuchsia::net::mdns::ServiceInstanceResolver>(
      fit::bind_member<&BindingSet<fuchsia::net::mdns::ServiceInstanceResolver>::OnBindRequest>(
          &service_instance_resolver_bindings_));
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
    FX_LOGS(FATAL) << "Invalid config file(s), terminating: " << config_.error();
    return;
  }

  auto interfaces_state = component_context_->svc()->Connect<fuchsia::net::interfaces::State>();
  fuchsia::net::interfaces::WatcherPtr watcher;
  interfaces_state->GetWatcher(fuchsia::net::interfaces::WatcherOptions(), watcher.NewRequest());
  mdns_.Start(std::move(watcher), host_name, config_.addresses(), config_.perform_host_name_probe(),
              fit::bind_member<&MdnsServiceImpl::OnReady>(this));
}

void MdnsServiceImpl::OnReady() {
  deprecated_services_.OnReady();
  ready_ = true;

  // Publish as indicated in config files.
  for (auto& publication : config_.publications()) {
    PublishServiceInstance(
        publication.service_, publication.instance_, publication.publication_->Clone(),
        publication.perform_probe_, publication.media_,
        [service = publication.service_](fpromise::result<void, fuchsia::net::mdns::Error> result) {
          if (result.is_error()) {
            FX_LOGS(ERROR) << "Failed to publish as " << service << ", result "
                           << static_cast<uint32_t>(result.error());
          }
        });
  }

  service_instance_resolver_bindings_.OnReady();
}

bool MdnsServiceImpl::PublishServiceInstance(
    std::string service_name, std::string instance_name,
    std::unique_ptr<Mdns::Publication> publication, bool perform_probe, Media media,
    fit::function<void(fpromise::result<void, fuchsia::net::mdns::Error>)> callback) {
  auto publisher = std::make_unique<SimplePublisher>(std::move(publication), callback.share());

  if (!mdns_.PublishServiceInstance(service_name, instance_name, perform_probe, media,
                                    publisher.get())) {
    return false;
  }

  std::string instance_full_name = MdnsNames::LocalInstanceFullName(instance_name, service_name);

  // |Mdns| told us our instance is unique locally, so the full name should
  // not appear in our collection.
  FX_DCHECK(publishers_by_instance_full_name_.find(instance_full_name) ==
            publishers_by_instance_full_name_.end());

  publishers_by_instance_full_name_.emplace(instance_full_name, std::move(publisher));

  return true;
}

void MdnsServiceImpl::ResolveServiceInstance(std::string service, std::string instance,
                                             int64_t timeout,
                                             ResolveServiceInstanceCallback callback) {
  if (!MdnsNames::IsValidServiceName(service)) {
    FX_LOGS(ERROR) << "ResolveServiceInstance called with invalid service name " << service;
    fuchsia::net::mdns::ServiceInstanceResolver_ResolveServiceInstance_Result result;
    result.set_err(fuchsia::net::mdns::Error::INVALID_SERVICE_NAME);
    callback(std::move(result));
    return;
  }

  if (!MdnsNames::IsValidInstanceName(instance)) {
    FX_LOGS(ERROR) << "ResolveServiceInstance called with invalid instance name " << instance;
    fuchsia::net::mdns::ServiceInstanceResolver_ResolveServiceInstance_Result result;
    result.set_err(fuchsia::net::mdns::Error::INVALID_INSTANCE_NAME);
    callback(std::move(result));
    return;
  }

  mdns_.ResolveServiceInstance(
      service, instance, zx::clock::get_monotonic() + zx::nsec(timeout),
      [callback = std::move(callback)](fuchsia::net::mdns::ServiceInstance instance) {
        fuchsia::net::mdns::ServiceInstanceResolver_ResolveServiceInstance_Result result;
        result.set_response(
            fuchsia::net::mdns::ServiceInstanceResolver_ResolveServiceInstance_Response(
                std::move(instance)));
        callback(std::move(result));
      });
}

////////////////////////////////////////////////////////////////////////////////
// MdnsServiceImpl::SimplePublisher implementation

MdnsServiceImpl::SimplePublisher::SimplePublisher(
    std::unique_ptr<Mdns::Publication> publication,
    fit::function<void(fpromise::result<void, fuchsia::net::mdns::Error>)> callback)
    : publication_(std::move(publication)), callback_(std::move(callback)) {}

void MdnsServiceImpl::SimplePublisher::ReportSuccess(bool success) {
  if (success) {
    callback_(fpromise::ok());
  } else {
    callback_(fpromise::error(fuchsia::net::mdns::Error::ALREADY_PUBLISHED_ON_SUBNET));
  }
}

void MdnsServiceImpl::SimplePublisher::GetPublication(
    Mdns::PublicationCause publication_cause, const std::string& subtype,
    const std::vector<inet::SocketAddress>& source_addresses, GetPublicationCallback callback) {
  FX_DCHECK(subtype.empty() || MdnsNames::IsValidSubtypeName(subtype));
  callback(publication_->Clone());
}

}  // namespace mdns
