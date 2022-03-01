// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_FORMATTERS_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_FORMATTERS_H_

#include <ostream>

#include "src/connectivity/network/mdns/service/common/reply_address.h"
#include "src/connectivity/network/mdns/service/common/types.h"

namespace mdns {

std::ostream& operator<<(std::ostream& os, const Media& value);
std::ostream& operator<<(std::ostream& os, const ReplyAddress& value);

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_FORMATTERS_H_
