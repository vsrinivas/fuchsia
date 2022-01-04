// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_NETIFC_DISCOVER_H_
#define SRC_BRINGUP_BIN_NETSVC_NETIFC_DISCOVER_H_

#include <lib/stdcompat/string_view.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>

#include "src/bringup/bin/netsvc/inet6.h"

struct DiscoveredInterface {
  zx::channel chan;
  mac_addr_t mac;
};

zx::status<DiscoveredInterface> netifc_discover(const std::string& ethdir,
                                                cpp17::string_view topological_path);

#endif  // SRC_BRINGUP_BIN_NETSVC_NETIFC_DISCOVER_H_
