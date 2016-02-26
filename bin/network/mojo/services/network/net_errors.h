// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_NETWORK_NET_ERRORS_H_
#define MOJO_SERVICES_NETWORK_NET_ERRORS_H_

#include <string>

namespace net {

enum Error {
  // No error.
  OK = 0,

#define NET_ERROR(label, value) ERR_ ## label = value,
#include "mojo/services/network/net_error_list.h"
#undef NET_ERROR
};

// Returns a textual representation of the error code for logging purposes.
std::string ErrorToString(int error);

// Same as above, but leaves off the leading "net::".
std::string ErrorToShortString(int error);

}  // namespace net

#endif /* MOJO_SERVICES_NETWORK_NET_ERRORS_H_ */
