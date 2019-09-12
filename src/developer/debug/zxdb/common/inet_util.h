// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_INET_UTIL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_INET_UTIL_H_

#include <stdint.h>

#include <string>

#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

// Parse |in_host,in_port|, storing the parsed values in |*out_host,*out_port|.
// The only parsing done for |in_host| is to ensure it's non-empty, and if it is an IPv6 address
// (i.e., it's wrapped in [], e.g., [::1]), then remove the outer [].
Err ParseHostPort(const std::string& in_host, const std::string& in_port, std::string* out_host,
                  uint16_t* out_port);

// Parse |input| as "host:port", storing the parsed values in |*out_host,*out_port|.
// If "host" is an IPv6 address it must be wrapped in [].
Err ParseHostPort(const std::string& input, std::string* out_host, uint16_t* out_port);

// Return true if |input| looks like "ipv6:port" with "ipv6" not wrapped in brackets.
// Return false in all other cases.
bool Ipv6HostPortIsMissingBrackets(const std::string& input);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_INET_UTIL_H_
