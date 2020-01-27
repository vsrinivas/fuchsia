// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/system_monitor/bin/harvester/dockyard_proxy.h"

#include "src/lib/fxl/logging.h"

namespace harvester {

std::string DockyardErrorString(const std::string& cmd,
                                DockyardProxyStatus err) {
  std::ostringstream os;
  os << cmd << " returned " << err;
  return os.str();
}

std::ostream& operator<<(std::ostream& out, const DockyardProxyStatus& status) {
  switch (status) {
    case DockyardProxyStatus::OK:
      return out << "OK (0)";
    case DockyardProxyStatus::ERROR:
      return out << "ERROR (-1)";
  }
  FXL_NOTREACHED();
  return out;
}

}  // namespace harvester.
