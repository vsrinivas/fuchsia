// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/core_dev_tools/system_info.h"

#include <sys/utsname.h>

#include "src/lib/fxl/strings/substitute.h"

namespace analytics {

std::string GetOsVersion() {
  struct utsname name;
  if (uname(&name) != 0) {
    return "unknown";
  }
  return fxl::Substitute("$0 $1", name.sysname, name.machine);
}

}  // namespace analytics
