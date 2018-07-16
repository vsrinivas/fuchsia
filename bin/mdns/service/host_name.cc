// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/host_name.h"

#include <fuchsia/netstack/cpp/fidl.h>
#include <limits.h>
#include <unistd.h>

#include "garnet/bin/mdns/service/mdns_fidl_util.h"
#include "garnet/bin/mdns/service/socket_address.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"

namespace mdns {
namespace {

static const std::string kFuchsia = "fuchsia";

class NetstackClient {
 public:
  static void GetInterfaces(
      fuchsia::netstack::Netstack::GetInterfacesCallback callback) {
    NetstackClient* client = new NetstackClient();
    client->netstack_->GetInterfaces(
        [client, callback = std::move(callback)](
            fidl::VectorPtr<fuchsia::netstack::NetInterface> interfaces) {
          callback(std::move(interfaces));
          delete client;
        });
  }

 private:
  NetstackClient()
      : context_(component::StartupContext::CreateFromStartupInfo()) {
    FXL_DCHECK(context_);
    netstack_ =
        context_->ConnectToEnvironmentService<fuchsia::netstack::Netstack>();
    FXL_DCHECK(netstack_);
  }

  std::unique_ptr<component::StartupContext> context_;
  fuchsia::netstack::NetstackPtr netstack_;
};

// Returns a host address, preferably V4. Returns an invalid address if no
// network interface could be found or if the interface hasn't obtained an
// address.
IpAddress GetHostAddress() {
  static IpAddress ip_address;
  if (ip_address)
    return ip_address;

  NetstackClient::GetInterfaces(
      [](const fidl::VectorPtr<fuchsia::netstack::NetInterface>& interfaces) {
        for (const auto& interface : *interfaces) {
          if (interface.addr.family ==
              fuchsia::netstack::NetAddressFamily::IPV4) {
            ip_address = MdnsFidlUtil::IpAddressFrom(&interface.addr);
            break;
          }
          if (interface.addr.family ==
              fuchsia::netstack::NetAddressFamily::IPV6) {
            ip_address = MdnsFidlUtil::IpAddressFrom(&interface.addr);
            // Keep looking...v4 is preferred.
          }
        }
      });

  return IpAddress::kInvalid;
}

}  // namespace

bool NetworkIsReady() { return GetHostAddress().is_valid(); }

// TODO: this should probably be an asynchronous interface.
std::string GetHostName() {
  char host_name_buffer[HOST_NAME_MAX + 1];
  int result = gethostname(host_name_buffer, sizeof(host_name_buffer));

  std::string host_name;

  if (result < 0) {
    FXL_LOG(ERROR) << "gethostname failed, errno " << errno;
    host_name = kFuchsia;
  } else {
    host_name = host_name_buffer;
  }

  // TODO(dalesat): Just use gethostname when NET-79 is fixed.

  if (host_name == kFuchsia) {
    // Seems we have the hard-coded host name. Supplement it with part of the
    // IP address.
    IpAddress address = GetHostAddress();
    if (address) {
      uint16_t suffix = address.is_v4()
                            ? static_cast<uint16_t>(
                                  address.as_bytes()[address.byte_count() - 1])
                            : address.as_words()[address.word_count() - 1];
      std::ostringstream os;
      os << host_name << "-" << suffix;
      host_name = os.str();
    }
  }

  return host_name;
}

}  // namespace mdns
