// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/host_name.h"

#include <limits.h>
#include <unistd.h>

#include "apps/netconnector/src/socket_address.h"
#include "apps/netstack/apps/include/netconfig.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"

namespace netconnector {
namespace {

static const std::string kFuchsia = "fuchsia";

bool AddressIsSet(const IpAddress& address) {
  size_t word_count = address.word_count();
  const uint16_t* words = address.as_words();

  for (size_t i = 0; i < word_count; ++i, ++words) {
    if (*words != 0) {
      return true;
    }
  }

  return false;
}

// Returns a host address, preferably V4. Returns an invalid address if no
// network interface could be found or if the interface hasn't obtained an
// address.
IpAddress GetHostAddress() {
  ftl::UniqueFD socket_fd = ftl::UniqueFD(socket(AF_INET, SOCK_DGRAM, 0));
  if (!socket_fd.is_valid()) {
    return IpAddress::kInvalid;
  }

  netc_get_if_info_t get_if_info;
  ssize_t size = ioctl_netc_get_if_info(socket_fd.get(), &get_if_info);
  if (size < 0) {
    return IpAddress::kInvalid;
  }

  if (get_if_info.n_info == 0) {
    return IpAddress::kInvalid;
  }

  IpAddress ip_address;

  for (uint32_t i = 0; i < get_if_info.n_info; ++i) {
    netc_if_info_t* if_info = &get_if_info.info[i];

    SocketAddress socket_address(if_info->addr);
    ip_address = socket_address.address();

    if (!AddressIsSet(ip_address)) {
      ip_address = IpAddress::kInvalid;
      continue;
    }

    if (ip_address.is_v4()) {
      break;
    }

    // Keep looking...v4 is preferred.
  }

  return ip_address;
}

}  // namespace

bool NetworkIsReady() {
  return GetHostAddress().is_valid();
}

std::string GetHostName() {
  char host_name_buffer[HOST_NAME_MAX + 1];
  int result = gethostname(host_name_buffer, sizeof(host_name_buffer));

  std::string host_name;

  if (result < 0) {
    FTL_LOG(ERROR) << "gethostname failed, errno " << errno;
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
