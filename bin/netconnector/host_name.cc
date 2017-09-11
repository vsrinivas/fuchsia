// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/host_name.h"

#include <limits.h>
#include <unistd.h>

#include "lib/app/cpp/application_context.h"
#include "garnet/bin/netconnector/socket_address.h"
#include "garnet/go/src/netstack/apps/include/netconfig.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"
#include "lib/netstack/fidl/netstack.fidl.h"

namespace netconnector {
namespace {

static const std::string kFuchsia = "fuchsia";

class NetstackClient {
 public:
  static void GetInterfaces(
      const netstack::Netstack::GetInterfacesCallback& callback) {
    NetstackClient* client = new NetstackClient();
    client->netstack_->GetInterfaces(
        [client, callback](fidl::Array<netstack::NetInterfacePtr> interfaces) {
          callback(std::move(interfaces));
          delete client;
        });
  }

 private:
  NetstackClient()
      : context_(app::ApplicationContext::CreateFromStartupInfo()) {
    FXL_DCHECK(context_);
    netstack_ = context_->ConnectToEnvironmentService<netstack::Netstack>();
    FXL_DCHECK(netstack_);
  }

  std::unique_ptr<app::ApplicationContext> context_;
  netstack::NetstackPtr netstack_;
};

// Returns a host address, preferably V4. Returns an invalid address if no
// network interface could be found or if the interface hasn't obtained an
// address.
IpAddress GetHostAddress() {
  static IpAddress ip_address;
  if (ip_address)
    return ip_address;

  NetstackClient::GetInterfaces(
      [](const fidl::Array<netstack::NetInterfacePtr>& interfaces) {
        for (const auto& interface : interfaces) {
          if (interface->addr->family == netstack::NetAddressFamily::IPV4) {
            ip_address = IpAddress(interface->addr.get());
            break;
          }
          if (interface->addr->family == netstack::NetAddressFamily::IPV6) {
            ip_address = IpAddress(interface->addr.get());
            // Keep looking...v4 is preferred.
          }
        }
      });

  return IpAddress::kInvalid;
}

}  // namespace

bool NetworkIsReady() {
  return GetHostAddress().is_valid();
}

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

}  // namespace netconnector
