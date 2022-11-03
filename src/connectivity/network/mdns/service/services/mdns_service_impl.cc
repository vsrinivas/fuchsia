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

#include "src/connectivity/network/mdns/service/common/mdns_fidl_util.h"
#include "src/connectivity/network/mdns/service/common/mdns_names.h"
#include "src/connectivity/network/mdns/service/common/type_converters.h"
#include "src/connectivity/network/mdns/service/services/host_name_resolver_service_impl.h"
#include "src/connectivity/network/mdns/service/services/host_name_subscriber_service_impl.h"
#include "src/connectivity/network/mdns/service/services/proxy_host_publisher_service_impl.h"
#include "src/connectivity/network/mdns/service/services/service_instance_publisher_service_impl.h"
#include "src/connectivity/network/mdns/service/services/service_instance_resolver_service_impl.h"
#include "src/connectivity/network/mdns/service/services/service_subscriber_service_impl.h"

namespace mdns {
namespace {

static constexpr zx::duration kReadyPollingInterval = zx::sec(1);

std::string GetLocalHostName() {
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
      mdns_(transceiver_),
      deprecated_services_(mdns_, component_context),
      host_name_resolver_manager_(
          [this](fidl::InterfaceRequest<fuchsia::net::mdns::HostNameResolver> request,
                 fit::closure deleter) {
            return std::make_unique<HostNameResolverServiceImpl>(mdns_, std::move(request),
                                                                 std::move(deleter));
          }),
      host_name_subscriber_manager_(
          [this](fidl::InterfaceRequest<fuchsia::net::mdns::HostNameSubscriber> request,
                 fit::closure deleter) {
            return std::make_unique<HostNameSubscriberServiceImpl>(mdns_, std::move(request),
                                                                   std::move(deleter));
          }),
      proxy_host_publisher_manager_(
          [this](fidl::InterfaceRequest<fuchsia::net::mdns::ProxyHostPublisher> request,
                 fit::closure deleter) {
            return std::make_unique<ProxyHostPublisherServiceImpl>(mdns_, std::move(request),
                                                                   std::move(deleter));
          }),
      service_instance_publisher_manager_(
          [this](fidl::InterfaceRequest<fuchsia::net::mdns::ServiceInstancePublisher> request,
                 fit::closure deleter) {
            return std::make_unique<ServiceInstancePublisherServiceImpl>(mdns_, std::move(request),
                                                                         std::move(deleter));
          }),
      service_subscriber_manager_(
          [this](fidl::InterfaceRequest<fuchsia::net::mdns::ServiceSubscriber2> request,
                 fit::closure deleter) {
            return std::make_unique<ServiceSubscriberServiceImpl>(mdns_, std::move(request),
                                                                  std::move(deleter));
          }),
      service_instance_resolver_manager_(
          [this](fidl::InterfaceRequest<fuchsia::net::mdns::ServiceInstanceResolver> request,
                 fit::closure deleter) {
            return std::make_unique<ServiceInstanceResolverServiceImpl>(mdns_, std::move(request),
                                                                        std::move(deleter));
          }) {
  host_name_resolver_manager_.AddOutgoingPublicService(component_context);
  host_name_subscriber_manager_.AddOutgoingPublicService(component_context);
  proxy_host_publisher_manager_.AddOutgoingPublicService(component_context);
  service_instance_publisher_manager_.AddOutgoingPublicService(component_context);
  service_subscriber_manager_.AddOutgoingPublicService(component_context);
  service_instance_resolver_manager_.AddOutgoingPublicService(component_context);

  Start();
}

MdnsServiceImpl::~MdnsServiceImpl() {}

void MdnsServiceImpl::Start() {
  std::string local_host_name = GetLocalHostName();

  if (local_host_name == fuchsia::device::DEFAULT_DEVICE_NAME) {
    // Host name not set. Try again soon.
    async::PostDelayedTask(
        async_get_default_dispatcher(), [this]() { Start(); }, kReadyPollingInterval);
    return;
  }

  config_.ReadConfigFiles(local_host_name);
  if (!config_.valid()) {
    FX_LOGS(FATAL) << "Invalid config file(s), terminating: " << config_.error();
    return;
  }

  auto interfaces_state = component_context_->svc()->Connect<fuchsia::net::interfaces::State>();
  fuchsia::net::interfaces::WatcherPtr watcher;
  interfaces_state->GetWatcher(fuchsia::net::interfaces::WatcherOptions(), watcher.NewRequest());
  mdns_.Start(std::move(watcher), local_host_name, config_.perform_host_name_probe(),
              fit::bind_member<&MdnsServiceImpl::OnReady>(this), config_.alt_services());
}

void MdnsServiceImpl::OnReady() {
  deprecated_services_.OnReady();
  ready_ = true;

  // Publish as indicated in config files.
  for (const auto& publication : config_.publications()) {
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

  host_name_resolver_manager_.OnReady();
  host_name_subscriber_manager_.OnReady();
  proxy_host_publisher_manager_.OnReady();
  service_instance_publisher_manager_.OnReady();
  service_subscriber_manager_.OnReady();
  service_instance_resolver_manager_.OnReady();
}

bool MdnsServiceImpl::PublishServiceInstance(
    std::string service_name, std::string instance_name,
    std::unique_ptr<Mdns::Publication> publication, bool perform_probe, Media media,
    fit::function<void(fpromise::result<void, fuchsia::net::mdns::Error>)> callback) {
  auto publisher = std::make_unique<SimplePublisher>(std::move(publication), callback.share());

  if (!mdns_.PublishServiceInstance(service_name, instance_name, media, IpVersions::kBoth,
                                    perform_probe, publisher.get())) {
    return false;
  }

  publishers_.push_back(std::move(publisher));
  return true;
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
    PublicationCause publication_cause, const std::string& subtype,
    const std::vector<inet::SocketAddress>& source_addresses, GetPublicationCallback callback) {
  FX_DCHECK(subtype.empty() || MdnsNames::IsValidSubtypeName(subtype));
  callback(publication_->Clone());
}

}  // namespace mdns
