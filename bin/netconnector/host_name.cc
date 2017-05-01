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

// TODO(dalesat): Just use gethostname when it can be relied upon.
static const std::string kFuchsia = "fuchsia";

}  // namespace

std::string GetHostName() {
  char host_name_buffer[HOST_NAME_MAX + 1];
  int result = gethostname(host_name_buffer, sizeof(host_name_buffer));

  std::string host_name;

  if (result < 0) {
    FTL_LOG(WARNING) << "gethostname failed, errno " << errno;
    host_name = kFuchsia;
  } else {
    host_name = host_name_buffer;
  }

  if (host_name == kFuchsia) {
    FTL_LOG(WARNING) << "gethostname returned '" << kFuchsia
                     << "', which is presumed to be a hard-coded value.";

    // Seems we have the hard-coded host name. Supplement it with part of the
    // IP address.
    ftl::UniqueFD socket_fd = ftl::UniqueFD(socket(AF_INET, SOCK_DGRAM, 0));
    if (!socket_fd.is_valid()) {
      FTL_LOG(WARNING) << "Failed to open socket, errno " << errno;
      FTL_LOG(WARNING)
          << "No IP address could be obtained to dedup the host name.";
      return host_name;
    }

    netc_get_if_info_t get_if_info;
    ssize_t size = ioctl_netc_get_if_info(socket_fd.get(), &get_if_info);
    if (size < 0) {
      FTL_LOG(WARNING) << "ioctl_netc_get_if_info failed, errno " << errno;
      FTL_LOG(WARNING)
          << "No IP address could be obtained to dedup the host name.";
      return host_name;
    }

    if (get_if_info.n_info == 0) {
      FTL_LOG(WARNING) << "ioctl_netc_get_if_info returned no interfaces";
      FTL_LOG(WARNING)
          << "No IP address could be obtained to dedup the host name.";
      return host_name;
    }

    for (uint32_t i = 0; i < get_if_info.n_info; ++i) {
      netc_if_info_t* if_info = &get_if_info.info[i];

      SocketAddress socket_address(if_info->addr);
      IpAddress ip_address = socket_address.address();

      if (ip_address.is_v4()) {
        std::ostringstream os;
        os << host_name << "-"
           << static_cast<int>(
                  ip_address.as_bytes()[ip_address.byte_count() - 1]);
        host_name = os.str();
        break;
      }

      std::ostringstream os;
      os << host_name << "-"
         << ip_address.as_words()[ip_address.word_count() - 1];
      host_name = os.str();
      // Keep looking...v4 is preferred.
    }

    FTL_LOG(WARNING)
        << "Part of the IP address has been appended to dedup the host name.";
  }

  return host_name;
}

}  // namespace netconnector
