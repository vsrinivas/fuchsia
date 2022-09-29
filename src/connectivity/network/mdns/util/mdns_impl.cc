// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/util/mdns_impl.h"

#include <arpa/inet.h>
#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zircon/status.h>

#include <iostream>
#include <unordered_set>

#include "lib/fidl/cpp/type_converter.h"
#include "src/connectivity/network/mdns/util/commands.h"
#include "src/connectivity/network/mdns/util/formatting.h"
#include "src/lib/fsl/types/type_converters.h"
#include "src/lib/inet/ip_address.h"

namespace fidl {

template <>
struct TypeConverter<fuchsia::net::IpAddress, inet::IpAddress> {
  static fuchsia::net::IpAddress Convert(const inet::IpAddress& value) {
    return static_cast<fuchsia::net::IpAddress>(value);
  }
};

template <>
struct TypeConverter<std::vector<uint8_t>, std::string> {
  static std::vector<uint8_t> Convert(const std::string& value) {
    return std::vector<uint8_t>(value.data(), value.data() + value.size());
  }
};

template <typename T, typename U>
struct TypeConverter<std::vector<T>, std::vector<U>> {
  static std::vector<T> Convert(const std::vector<U>& value) {
    std::vector<T> result;
    std::transform(value.begin(), value.end(), std::back_inserter(result),
                   [](const U& u) { return fidl::To<T>(u); });
    return result;
  }
};

}  // namespace fidl

namespace mdns {
namespace {

const std::string kPrompt = "[mdns-util] ";
const std::string kHostSuffix = ".local.";

}  // namespace

MdnsImpl::MdnsImpl(sys::ComponentContext* component_context, Command command,
                   async_dispatcher_t* dispatcher, fit::closure quit_callback)
    : component_context_(component_context),
      dispatcher_(dispatcher),
      quit_callback_(std::move(quit_callback)) {
  FX_DCHECK(component_context);
  FX_DCHECK(quit_callback_);

  // Based on the command entered on the command line, determine whether to run interactively.
  switch (command.verb()) {
    case CommandVerb::kResolveHost:
    case CommandVerb::kResolveInstance:
      // The command makes sense to run without interaction. Terminate when it completes.
      transient_ = true;
      break;

    case CommandVerb::kSubscribeHost:
    case CommandVerb::kSubscribeService:
    case CommandVerb::kPublishHost:
    case CommandVerb::kPublishInstance:
    case CommandVerb::kEmpty:
      // The command implies that the utility should run interactively.
      transient_ = false;
      break;

    case CommandVerb::kUnsubscribeHost:
    case CommandVerb::kUnsubscribeService:
    case CommandVerb::kUnpublishHost:
    case CommandVerb::kUnpublishInstance:
    case CommandVerb::kQuit:
      // The command makes no sense on the command line. Show help and quit.
      std::cout << "error: command is not valid on the command line\n";
      Command::ShowHelp(CommandVerb::kHelp);
      quit_callback_();
      return;

    case CommandVerb::kHelp:
    case CommandVerb::kMalformed:
      // The command is an explicit request for help or is malformed. Show help and quit.
      command.ShowHelp();
      quit_callback_();
      return;
  }

  ExecuteCommand(command);

  if (transient_) {
    return;
  }

  input_.SetEofCallback([this]() { Quit(); });

  input_.Init(
      [this](std::string command_line) {
        CommandParser parser(command_line);
        HideInput();
        ExecuteCommand(parser.Parse());
        ShowInput();
        input_.AddToHistory(command_line);
      },
      kPrompt);

  ShowInput();

  WaitForKeystroke();
}

void MdnsImpl::ExecuteCommand(const Command& command) {
  switch (command.verb()) {
    case CommandVerb::kResolveHost:
      ResolveHost(command.host_name(), command.timeout(), command.media(), command.ip_versions(),
                  command.exclude_local(), command.exclude_local_proxies());
      break;
    case CommandVerb::kResolveInstance:
      ResolveInstance(command.instance_name(), command.service_name(), command.timeout(),
                      command.media(), command.ip_versions(), command.exclude_local(),
                      command.exclude_local_proxies());
      break;
    case CommandVerb::kSubscribeHost:
      SubscribeHost(command.host_name(), command.media(), command.ip_versions(),
                    command.exclude_local(), command.exclude_local_proxies());
      break;
    case CommandVerb::kSubscribeService:
      SubscribeService(command.service_name(), command.media(), command.ip_versions(),
                       command.exclude_local(), command.exclude_local_proxies());
      break;
    case CommandVerb::kPublishHost:
      PublishHost(command.host_name(), command.addresses(), command.probe(), command.media(),
                  command.ip_versions());
      break;
    case CommandVerb::kPublishInstance:
      PublishInstance(command.instance_name(), command.service_name(), command.port(),
                      command.text(), command.probe(), command.media(), command.ip_versions(),
                      command.srv_priority(), command.srv_weight(), command.ptr_ttl(),
                      command.srv_ttl(), command.txt_ttl(), command.proxy_host_name());
      break;
    case CommandVerb::kUnsubscribeHost:
      UnsubscribeHost(command.host_name());
      break;
    case CommandVerb::kUnsubscribeService:
      UnsubscribeService(command.service_name());
      break;
    case CommandVerb::kUnpublishHost:
      UnpublishHost(command.host_name());
      break;
    case CommandVerb::kUnpublishInstance:
      UnpublishInstance(command.instance_name(), command.service_name(), command.proxy_host_name());
      break;
    case CommandVerb::kHelp:
    case CommandVerb::kMalformed:
      command.ShowHelp();
      break;

    case CommandVerb::kQuit:
      Quit();
      break;

    default:
      break;
  }
}

void MdnsImpl::WaitForKeystroke() {
  fd_waiter_.Wait(
      [this](zx_status_t status, uint32_t events) {
        char ch;
        if (read(STDIN_FILENO, &ch, 1) > 0) {
          input_.OnInput(ch);
        }

        WaitForKeystroke();
      },
      STDIN_FILENO, POLLIN);
}

void MdnsImpl::ResolveHost(const std::string& host_name, zx::duration timeout,
                           fuchsia::net::mdns::Media media,
                           fuchsia::net::mdns::IpVersions ip_versions, bool exclude_local,
                           bool exclude_local_proxies) {
  std::cout << Command::kResolve << " " << host_name << kHostSuffix << " starting\n";

  EnsureHostNameResolver();

  fuchsia::net::mdns::HostNameResolutionOptions options;
  options.set_media(media);
  options.set_ip_versions(ip_versions);
  options.set_exclude_local(exclude_local);
  options.set_exclude_local_proxies(exclude_local_proxies);

  host_name_resolver_->ResolveHostName(
      host_name, timeout.get(), std::move(options),
      [this, host_name](std::vector<fuchsia::net::mdns::HostAddress> addresses) {
        HideInput();

        if (addresses.empty()) {
          std::cout << Command::kResolve << " " << host_name << kHostSuffix
                    << " failed: host not found\n";
          ShowInput();
          QuitIfTransient();
          return;
        }

        std::cout << Command::kResolve << " " << host_name << kHostSuffix << " succeeded\n";
        for (auto& address : addresses) {
          std::cout << "    " << inet::IpAddress(address.address) << " interface "
                    << address.interface << " ttl " << zx::duration(address.ttl).to_secs() << "s\n";
        }

        ShowInput();
        QuitIfTransient();
      });
}

void MdnsImpl::ResolveInstance(const std::string& instance_name, const std::string& service_name,
                               zx::duration timeout, fuchsia::net::mdns::Media media,
                               fuchsia::net::mdns::IpVersions ip_versions, bool exclude_local,
                               bool exclude_local_proxies) {
  std::cout << Command::kResolve << " " << instance_name << "." << service_name << " starting\n";

  EnsureServiceInstanceResolver();

  fuchsia::net::mdns::ServiceInstanceResolutionOptions options;
  options.set_media(media);
  options.set_ip_versions(ip_versions);
  options.set_exclude_local(exclude_local);
  options.set_exclude_local_proxies(exclude_local_proxies);

  service_instance_resolver_->ResolveServiceInstance(
      service_name, instance_name, timeout.get(), std::move(options),
      [this, instance_name, service_name](fuchsia::net::mdns::ServiceInstance instance) {
        HideInput();

        if (!instance.has_addresses() || instance.addresses().empty()) {
          std::cout << Command::kResolve << " " << instance_name << "." << service_name
                    << " failed: instance not found\n";
          ShowInput();
          QuitIfTransient();
          return;
        }

        std::cout << Command::kResolve << " " << instance_name << "." << service_name
                  << " succeeded:" << instance << fostr::NewLine;

        ShowInput();
        QuitIfTransient();
      });
}

void MdnsImpl::SubscribeHost(const std::string& host_name, fuchsia::net::mdns::Media media,
                             fuchsia::net::mdns::IpVersions ip_versions, bool exclude_local,
                             bool exclude_local_proxies) {
  if (host_name_subscription_listeners_by_host_name_.find(host_name) !=
      host_name_subscription_listeners_by_host_name_.end()) {
    std::cout << Command::kSubscribe << " " << host_name << kHostSuffix
              << " failed: already subscribed\n";
    return;
  }

  std::cout << Command::kSubscribe << " " << host_name << kHostSuffix << " starting\n";

  EnsureHostNameSubscriber();

  fuchsia::net::mdns::HostNameSubscriptionOptions options;
  options.set_media(media);
  options.set_ip_versions(ip_versions);
  options.set_exclude_local(exclude_local);
  options.set_exclude_local_proxies(exclude_local_proxies);

  fidl::InterfaceHandle<fuchsia::net::mdns::HostNameSubscriptionListener> listener_handle;
  auto listener = std::make_unique<HostNameSubscriptionListener>(
      host_name, listener_handle.NewRequest(), input_);

  auto listener_raw = listener.get();
  host_name_subscription_listeners_by_host_name_.emplace(host_name, std::move(listener));

  listener_raw->set_error_handler([this, host_name](zx_status_t status) mutable {
    HideInput();
    std::cout << Command::kSubscribe << " " << host_name << kHostSuffix
              << " listener channel disconnected unexpectedly, " << zx_status_get_string(status)
              << "\n";
    ShowInput();
    host_name_subscription_listeners_by_host_name_.erase(host_name);
  });

  host_name_subscriber_->SubscribeToHostName(host_name, std::move(options),
                                             std::move(listener_handle));
}

void MdnsImpl::SubscribeService(const std::string& service_name, fuchsia::net::mdns::Media media,
                                fuchsia::net::mdns::IpVersions ip_versions, bool exclude_local,
                                bool exclude_local_proxies) {
  if (service_subscription_listeners_by_service_name_.find(service_name) !=
      service_subscription_listeners_by_service_name_.end()) {
    std::cout << Command::kSubscribe << " " << service_name << " failed: already subscribed\n";
    return;
  }

  std::cout << Command::kSubscribe << " " << service_name << " starting\n";

  EnsureServiceSubscriber();

  fuchsia::net::mdns::ServiceSubscriptionOptions options;
  options.set_media(media);
  options.set_ip_versions(ip_versions);
  options.set_exclude_local(exclude_local);
  options.set_exclude_local_proxies(exclude_local_proxies);

  fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriptionListener> listener_handle;
  auto listener = std::make_unique<ServiceSubscriptionListener>(
      service_name, listener_handle.NewRequest(), input_);

  auto listener_raw = listener.get();
  service_subscription_listeners_by_service_name_.emplace(service_name, std::move(listener));

  listener_raw->set_error_handler([this, service_name](zx_status_t status) mutable {
    HideInput();
    std::cout << Command::kSubscribe << " " << service_name
              << " listener channel disconnected unexpectedly, " << zx_status_get_string(status)
              << "\n";
    ShowInput();
    service_subscription_listeners_by_service_name_.erase(service_name);
  });

  if (service_name == Command::kAllServices) {
    service_subscriber_->SubscribeToAllServices(std::move(options), std::move(listener_handle));
  } else {
    service_subscriber_->SubscribeToService(service_name, std::move(options),
                                            std::move(listener_handle));
  }
}

void MdnsImpl::PublishHost(const std::string& host_name, std::vector<inet::IpAddress> addresses,
                           bool probe, fuchsia::net::mdns::Media media,
                           fuchsia::net::mdns::IpVersions ip_versions) {
  if (proxy_hosts_by_name_.find(host_name) != proxy_hosts_by_name_.end()) {
    std::cout << Command::kPublish << " " << host_name << kHostSuffix
              << " failed: already published\n";
    return;
  }

  std::cout << Command::kPublish << " " << host_name << kHostSuffix << " starting\n";

  EnsureProxyHostPublisher();

  fuchsia::net::mdns::ProxyHostPublicationOptions options;
  options.set_media(media);
  options.set_ip_versions(ip_versions);
  options.set_perform_probe(probe);

  fuchsia::net::mdns::ServiceInstancePublisherPtr service_instance_publisher;

  proxy_host_publisher_->PublishProxyHost(
      host_name, fidl::To<std::vector<fuchsia::net::IpAddress>>(addresses), std::move(options),
      service_instance_publisher.NewRequest(),
      [this, host_name, service_instance_publisher = std::move(service_instance_publisher)](
          fuchsia::net::mdns::ProxyHostPublisher_PublishProxyHost_Result result) mutable {
        HideInput();

        if (result.is_err()) {
          switch (result.err()) {
            case fuchsia::net::mdns::PublishProxyHostError::ALREADY_PUBLISHED_LOCALLY:
              std::cout << Command::kPublish << " " << host_name << kHostSuffix
                        << " failed: already published locally\n";
              break;
            case fuchsia::net::mdns::PublishProxyHostError::ALREADY_PUBLISHED_ON_SUBNET:
              std::cout << Command::kPublish << " " << host_name << kHostSuffix
                        << " failed: already published on subnet\n";
              break;
          }

          ShowInput();
          return;
        }

        std::cout << Command::kPublish << " " << host_name << kHostSuffix << " succeeded\n";

        auto proxy_host = std::make_unique<ProxyHost>(std::move(service_instance_publisher));
        proxy_host->set_error_handler([this, host_name](zx_status_t status) {
          HideInput();
          std::cout << Command::kPublish << " " << host_name << kHostSuffix
                    << " publisher disconnected unexpectedly, " << zx_status_get_string(status)
                    << "\n";
          ShowInput();
          proxy_hosts_by_name_.erase(host_name);
        });

        proxy_hosts_by_name_.emplace(host_name, std::move(proxy_host));

        ShowInput();
      });
}

void MdnsImpl::PublishInstance(const std::string& instance_name, const std::string& service_name,
                               uint16_t port, const std::vector<std::string>& text, bool probe,
                               fuchsia::net::mdns::Media media,
                               fuchsia::net::mdns::IpVersions ip_versions, uint16_t srv_priority,
                               uint16_t srv_weight, zx::duration ptr_ttl, zx::duration srv_ttl,
                               zx::duration txt_ttl, const std::string& proxy_host_name) {
  std::string instance_full_name = instance_name + "." + service_name;
  std::string title;

  ProxyHost* proxy_host = nullptr;

  if (proxy_host_name.empty()) {
    title = instance_full_name;
    auto iter =
        service_instance_publication_responders_by_instance_full_name_.find(instance_full_name);
    if (iter == service_instance_publication_responders_by_instance_full_name_.end()) {
      std::cout << Command::kPublish << " " << title << " failed: already published\n";
      return;
    }

    EnsureServiceInstancePublisher();
  } else {
    title = instance_full_name + " on proxy " + proxy_host_name;
    auto iter = proxy_hosts_by_name_.find(proxy_host_name);
    if (iter == proxy_hosts_by_name_.end()) {
      std::cout << Command::kPublish << " " << title << " failed: no such proxy\n";
      return;
    }

    proxy_host = iter->second.get();
    if (proxy_host->ResponderExists(instance_full_name)) {
      std::cout << Command::kPublish << " " << title << " failed: already published on the proxy\n";
      return;
    }
  }

  fuchsia::net::mdns::ServiceInstancePublicationOptions options;
  options.set_media(media);
  options.set_ip_versions(ip_versions);
  options.set_perform_probe(probe);

  fuchsia::net::mdns::ServiceInstancePublisherPtr& publisher =
      proxy_host ? proxy_host->publisher() : service_instance_publisher_;

  fuchsia::net::mdns::ServiceInstancePublication publication;
  publication.set_port(port);
  publication.set_text(fidl::To<std::vector<std::vector<uint8_t>>>(std::move(text)));
  publication.set_srv_priority(srv_priority);
  publication.set_srv_weight(srv_weight);
  publication.set_ptr_ttl(ptr_ttl.get());
  publication.set_srv_ttl(srv_ttl.get());
  publication.set_txt_ttl(txt_ttl.get());

  fidl::InterfaceHandle<fuchsia::net::mdns::ServiceInstancePublicationResponder> responder_handle;
  auto request = responder_handle.NewRequest();

  publisher->PublishServiceInstance(
      std::move(service_name), std::move(instance_name), std::move(options),
      std::move(responder_handle),
      [this, proxy_host, proxy_host_name, request = std::move(request),
       publication = std::move(publication), instance_full_name,
       title](fuchsia::net::mdns::ServiceInstancePublisher_PublishServiceInstance_Result
                  result) mutable {
        HideInput();

        if (result.is_err()) {
          switch (result.err()) {
            case fuchsia::net::mdns::PublishServiceInstanceError::ALREADY_PUBLISHED_LOCALLY:
              std::cout << Command::kPublish << " " << title
                        << " failed: already published locally\n";
              break;
            case fuchsia::net::mdns::PublishServiceInstanceError::ALREADY_PUBLISHED_ON_SUBNET:
              std::cout << Command::kPublish << " " << title
                        << " failed: already published on subnet\n";
              break;
          }

          ShowInput();
          return;
        }

        std::cout << Command::kPublish << " " << title << " starting\n";

        auto responder = std::make_unique<ServiceInstancePublicationResponder>(
            std::move(request), input_, std::move(publication));

        auto responder_raw = responder.get();
        if (proxy_host) {
          proxy_host->AddResponder(instance_full_name, std::move(responder));
        } else {
          service_instance_publication_responders_by_instance_full_name_.emplace(
              instance_full_name, std::move(responder));
        }

        responder_raw->set_error_handler(
            [this, instance_full_name, title, proxy_host_name](zx_status_t status) mutable {
              HideInput();

              std::cout << Command::kPublish << " " << title
                        << " responder channel disconnected unexpectedly, "
                        << zx_status_get_string(status) << "\n";

              if (proxy_host_name.empty()) {
                service_instance_publication_responders_by_instance_full_name_.erase(
                    instance_full_name);
              } else {
                auto iter = proxy_hosts_by_name_.find(proxy_host_name);
                if (iter != proxy_hosts_by_name_.end()) {
                  iter->second->RemoveResponder(instance_full_name);
                }
              }

              ShowInput();
            });

        ShowInput();
      });
}

void MdnsImpl::UnsubscribeHost(const std::string& host_name) {
  if (host_name_subscription_listeners_by_host_name_.erase(host_name) == 0) {
    std::cout << Command::kUnsubscribe << " " << host_name << kHostSuffix
              << " failed: not currenly subscribed\n";
    return;
  }

  std::cout << Command::kUnsubscribe << " " << host_name << kHostSuffix << " succeeded\n";
}

void MdnsImpl::UnsubscribeService(const std::string& service_name) {
  if (service_subscription_listeners_by_service_name_.erase(service_name) == 0) {
    std::cout << Command::kUnsubscribe << " " << service_name
              << " failed: not currenly subscribed\n";
    return;
  }

  std::cout << Command::kUnsubscribe << " " << service_name << " succeeded\n";
}

void MdnsImpl::UnpublishHost(const std::string& host_name) {
  if (proxy_hosts_by_name_.erase(host_name) == 0) {
    std::cout << Command::kUnpublish << " " << host_name << kHostSuffix
              << " failed: not currently published\n";
    return;
  }

  std::cout << Command::kUnpublish << " " << host_name << kHostSuffix << " succeeded\n";
}

void MdnsImpl::UnpublishInstance(const std::string& instance_name, const std::string& service_name,
                                 const std::string& proxy_host_name) {
  std::string instance_full_name = instance_name + "." + service_name;

  if (proxy_host_name.empty()) {
    if (service_instance_publication_responders_by_instance_full_name_.erase(instance_full_name) ==
        0) {
      std::cout << Command::kUnpublish << " " << instance_full_name
                << " failed: not currently published\n";
      return;
    }

    std::cout << Command::kUnpublish << " " << instance_full_name << " succeeded\n";
    return;
  }

  auto iter = proxy_hosts_by_name_.find(proxy_host_name);
  if (iter == proxy_hosts_by_name_.end()) {
    std::cout << Command::kUnpublish << " " << instance_full_name << " on proxy " << proxy_host_name
              << " failed: proxy host not currently published\n";
    return;
  }

  if (!iter->second->RemoveResponder(instance_full_name)) {
    std::cout << Command::kUnpublish << " " << instance_full_name << " on proxy " << proxy_host_name
              << " failed: not currently published on proxy host\n";
    return;
  }

  std::cout << Command::kUnpublish << " " << instance_full_name << " on proxy " << proxy_host_name
            << " succeeded\n";
}

void MdnsImpl::EnsureHostNameResolver() {
  if (host_name_resolver_) {
    return;
  }

  host_name_resolver_ = component_context_->svc()->Connect<fuchsia::net::mdns::HostNameResolver>();

  host_name_resolver_.set_error_handler([this](zx_status_t status) {
    HideInput();
    std::cout << "fuchsia.net.mdns.HostNameResolver channel disconnected unexpectedly, "
              << zx_status_get_string(status) << "\n";
    ShowInput();
  });
}

void MdnsImpl::EnsureServiceInstanceResolver() {
  if (service_instance_resolver_) {
    return;
  }

  service_instance_resolver_ =
      component_context_->svc()->Connect<fuchsia::net::mdns::ServiceInstanceResolver>();
  service_instance_resolver_.set_error_handler([this](zx_status_t status) {
    HideInput();
    std::cout << "fuchsia.net.mdns.ServiceInstanceResolver channel disconnected: "
              << zx_status_get_string(status);
    ShowInput();
  });
}

void MdnsImpl::EnsureHostNameSubscriber() {
  if (host_name_subscriber_) {
    return;
  }

  host_name_subscriber_ =
      component_context_->svc()->Connect<fuchsia::net::mdns::HostNameSubscriber>();

  host_name_subscriber_.set_error_handler([this](zx_status_t status) {
    HideInput();
    std::cout << "fuchsia.net.mdns.HostNameSubscriber channel disconnected unexpectedly, "
              << zx_status_get_string(status) << "\n";
    ShowInput();
  });
}

void MdnsImpl::EnsureServiceSubscriber() {
  if (service_subscriber_) {
    return;
  }

  service_subscriber_ =
      component_context_->svc()->Connect<fuchsia::net::mdns::ServiceSubscriber2>();

  service_subscriber_.set_error_handler([this](zx_status_t status) {
    HideInput();
    std::cout << "fuchsia.net.mdns.ServiceSubscriber2 channel disconnected unexpectedly, "
              << zx_status_get_string(status) << "\n";
    ShowInput();
  });
}

void MdnsImpl::EnsureProxyHostPublisher() {
  if (proxy_host_publisher_) {
    return;
  }

  proxy_host_publisher_ =
      component_context_->svc()->Connect<fuchsia::net::mdns::ProxyHostPublisher>();

  proxy_host_publisher_.set_error_handler([this](zx_status_t status) {
    HideInput();
    std::cout << "fuchsia.net.mdns.ProxyHostPublisher channel disconnected unexpectedly, "
              << zx_status_get_string(status) << "\n";
    ShowInput();
  });
}

void MdnsImpl::EnsureServiceInstancePublisher() {
  if (service_instance_publisher_) {
    return;
  }

  service_instance_publisher_ =
      component_context_->svc()->Connect<fuchsia::net::mdns::ServiceInstancePublisher>();

  service_instance_publisher_.set_error_handler([this](zx_status_t status) {
    HideInput();
    std::cout << "fuchsia.net.mdns.ServiceInstancePublisher channel disconnected unexpectedly, "
              << zx_status_get_string(status) << "\n";
    ShowInput();
  });
}

void MdnsImpl::Quit() {
  async::PostTask(dispatcher_, [this]() { QuitSync(); });
}

void MdnsImpl::QuitSync() {
  host_name_subscription_listeners_by_host_name_.clear();
  service_subscription_listeners_by_service_name_.clear();
  proxy_hosts_by_name_.clear();
  service_instance_publication_responders_by_instance_full_name_.clear();

  host_name_resolver_.set_error_handler(nullptr);
  service_instance_resolver_.set_error_handler(nullptr);
  host_name_subscriber_.set_error_handler(nullptr);
  service_subscriber_.set_error_handler(nullptr);
  proxy_host_publisher_.set_error_handler(nullptr);
  service_instance_publisher_.set_error_handler(nullptr);

  host_name_resolver_.Unbind();
  service_instance_resolver_.Unbind();
  host_name_subscriber_.Unbind();
  service_subscriber_.Unbind();
  proxy_host_publisher_.Unbind();
  service_instance_publisher_.Unbind();

  HideInput();

  quit_callback_();
}

void MdnsImpl::QuitIfTransient() {
  if (transient_) {
    Quit();
  }
}

void MdnsImpl::ShowInput() {
  if (!transient_) {
    input_.Show();
  }
}

void MdnsImpl::HideInput() {
  if (!transient_) {
    input_.Hide();
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// HostNameSubscriptionListener definitions.

MdnsImpl::HostNameSubscriptionListener::HostNameSubscriptionListener(
    std::string host_name,
    fidl::InterfaceRequest<fuchsia::net::mdns::HostNameSubscriptionListener> request,
    line_input::ModalLineInput& input)
    : host_name_(host_name), binding_(this, std::move(request)), input_(input) {}

void MdnsImpl::HostNameSubscriptionListener::set_error_handler(
    fit::function<void(zx_status_t)> error_handler) {
  binding_.set_error_handler(std::move(error_handler));
}

void MdnsImpl::HostNameSubscriptionListener::OnAddressesChanged(
    std::vector<fuchsia::net::mdns::HostAddress> addresses, OnAddressesChangedCallback callback) {
  input_.Hide();
  std::cout << Command::kSubscribe << " " << host_name_ << ": addresses" << addresses << "\n";
  input_.Show();
  callback();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// ServiceSubscriptionListener definitions.

MdnsImpl::ServiceSubscriptionListener::ServiceSubscriptionListener(
    const std::string& service_name,
    fidl::InterfaceRequest<fuchsia::net::mdns::ServiceSubscriptionListener> request,
    line_input::ModalLineInput& input)
    : service_name_(service_name), binding_(this, std::move(request)), input_(input) {}

void MdnsImpl::ServiceSubscriptionListener::set_error_handler(
    fit::function<void(zx_status_t)> error_handler) {
  binding_.set_error_handler(std::move(error_handler));
}

void MdnsImpl::ServiceSubscriptionListener::OnInstanceDiscovered(
    fuchsia::net::mdns::ServiceInstance instance, OnInstanceDiscoveredCallback callback) {
  input_.Hide();
  std::cout << Command::kSubscribe << " " << service_name_ << ": instance " << instance.instance()
            << "." << instance.service() << " discovered" << instance << "\n";
  input_.Show();
  callback();
}

void MdnsImpl::ServiceSubscriptionListener::OnInstanceChanged(
    fuchsia::net::mdns::ServiceInstance instance, OnInstanceChangedCallback callback) {
  input_.Hide();
  std::cout << Command::kSubscribe << " " << service_name_ << ": instance " << instance.instance()
            << "." << instance.service() << " changed" << instance << "\n";
  input_.Show();
  callback();
}

void MdnsImpl::ServiceSubscriptionListener::OnInstanceLost(std::string service_name,
                                                           std::string instance_name,
                                                           OnInstanceLostCallback callback) {
  input_.Hide();
  std::cout << Command::kSubscribe << " " << service_name_ << ": instance " << instance_name << "."
            << service_name << " lost\n";
  input_.Show();
  callback();
}

void MdnsImpl::ServiceSubscriptionListener::OnQuery(fuchsia::net::mdns::ResourceType resource_type,
                                                    OnQueryCallback callback) {
  // This method is just for metrics. Ignore it.
  callback();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// ServiceInstancePublicationResponder definitions.

MdnsImpl::ServiceInstancePublicationResponder::ServiceInstancePublicationResponder(
    fidl::InterfaceRequest<fuchsia::net::mdns::ServiceInstancePublicationResponder> request,
    line_input::ModalLineInput& input, fuchsia::net::mdns::ServiceInstancePublication publication)
    : binding_(this, std::move(request)), input_(input), publication_(std::move(publication)) {
  (void)input_;  // not used
}

void MdnsImpl::ServiceInstancePublicationResponder::set_error_handler(
    fit::function<void(zx_status_t)> error_handler) {
  binding_.set_error_handler(std::move(error_handler));
}

void MdnsImpl::ServiceInstancePublicationResponder::OnPublication(
    fuchsia::net::mdns::ServiceInstancePublicationCause publication_cause, fidl::StringPtr subtype,
    std::vector<fuchsia::net::IpAddress> source_addresses, OnPublicationCallback callback) {
  if (subtype.has_value()) {
    callback(fpromise::error(fuchsia::net::mdns::OnPublicationError::DO_NOT_RESPOND));
    return;
  }

  callback(fpromise::ok(fidl::Clone(publication_)));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// ProxyHost definitions.

MdnsImpl::ProxyHost::ProxyHost(fuchsia::net::mdns::ServiceInstancePublisherPtr publisher)
    : publisher_(std::move(publisher)) {}

void MdnsImpl::ProxyHost::set_error_handler(fit::function<void(zx_status_t)> error_handler) {
  publisher_.set_error_handler(std::move(error_handler));
}

bool MdnsImpl::ProxyHost::ResponderExists(std::string instance_full_name) {
  return service_instance_publication_responders_by_instance_full_name_.find(instance_full_name) !=
         service_instance_publication_responders_by_instance_full_name_.end();
}

void MdnsImpl::ProxyHost::AddResponder(
    std::string instance_full_name,
    std::unique_ptr<ServiceInstancePublicationResponder> responder) {
  service_instance_publication_responders_by_instance_full_name_.emplace(instance_full_name,
                                                                         std::move(responder));
}

bool MdnsImpl::ProxyHost::RemoveResponder(std::string instance_full_name) {
  return service_instance_publication_responders_by_instance_full_name_.erase(instance_full_name) ==
         1;
}

}  // namespace mdns
