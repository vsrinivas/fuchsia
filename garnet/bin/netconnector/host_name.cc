// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/host_name.h"

#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <limits.h>
#include <unistd.h>

#include "garnet/lib/inet/socket_address.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/logging.h"

namespace netconnector {
namespace {

static const std::string kFuchsia = "fuchsia-unset-device-name";

class NetstackClient {
 public:
  static void GetInterfaces(
      fuchsia::netstack::Netstack::GetInterfacesCallback callback) {
    NetstackClient* client = new NetstackClient();
    client->netstack_->GetInterfaces(
        [client, callback = std::move(callback)](
            std::vector<fuchsia::netstack::NetInterface> interfaces) {
          callback(std::move(interfaces));
          delete client;
        });
  }

 private:
  NetstackClient() : context_(sys::ComponentContext::Create()) {
    FXL_DCHECK(context_);
    netstack_ = context_->svc()->Connect<fuchsia::netstack::Netstack>();
    FXL_DCHECK(netstack_);
  }

  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::netstack::NetstackPtr netstack_;
};

// Returns a host address, preferably V4. Returns an invalid address if no
// network interface could be found or if the interface hasn't obtained an
// address.
inet::IpAddress GetHostAddress() {
  static inet::IpAddress ip_address;
  if (ip_address)
    return ip_address;

  NetstackClient::GetInterfaces(
      [](const std::vector<fuchsia::netstack::NetInterface>& interfaces) {
        for (const auto& interface : interfaces) {
          if (interface.addr.Which() == fuchsia::net::IpAddress::Tag::kIpv4) {
            ip_address = inet::IpAddress(&interface.addr);
            break;
          }
          if (interface.addr.Which() == fuchsia::net::IpAddress::Tag::kIpv6) {
            ip_address = inet::IpAddress(&interface.addr);
            // Keep looking...v4 is preferred.
          }
        }
      });

  return inet::IpAddress::kInvalid;
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
    inet::IpAddress address = GetHostAddress();
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
