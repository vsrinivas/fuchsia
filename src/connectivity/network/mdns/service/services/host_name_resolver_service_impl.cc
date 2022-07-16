// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/services/host_name_resolver_service_impl.h"

#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/network/mdns/service/common/mdns_names.h"
#include "src/connectivity/network/mdns/service/common/type_converters.h"

namespace mdns {

HostNameResolverServiceImpl::HostNameResolverServiceImpl(
    Mdns& mdns, fidl::InterfaceRequest<fuchsia::net::mdns::HostNameResolver> request,
    fit::closure deleter)
    : ServiceImplBase<fuchsia::net::mdns::HostNameResolver>(mdns, std::move(request),
                                                            std::move(deleter)) {}

void HostNameResolverServiceImpl::ResolveHostName(
    std::string host, int64_t timeout_ns, fuchsia::net::mdns::HostNameResolutionOptions options,
    ResolveHostNameCallback callback) {
  if (!MdnsNames::IsValidHostName(host)) {
    FX_LOGS(ERROR) << "ResolveHostName called with invalid host name " << host
                   << ", closing connection.";
    Quit();
    return;
  }

  Media media = options.has_media() ? fidl::To<Media>(options.media()) : Media::kBoth;
  IpVersions ip_versions =
      options.has_ip_versions() ? fidl::To<IpVersions>(options.ip_versions()) : IpVersions::kBoth;

  bool include_local = !options.has_exclude_local() || !options.exclude_local();
  bool include_local_proxies =
      !options.has_exclude_local_proxies() || !options.exclude_local_proxies();

  mdns().ResolveHostName(
      host, zx::nsec(timeout_ns), media, ip_versions, include_local, include_local_proxies,
      [callback = std::move(callback)](const std::string& host,
                                       std::vector<HostAddress> addresses) {
        callback(fidl::To<std::vector<fuchsia::net::mdns::HostAddress>>(addresses));
      });
}

}  // namespace mdns
