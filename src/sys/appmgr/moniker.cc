// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/moniker.h"

#include <string>
#include <vector>

#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/substitute.h"

namespace component {

std::string Moniker::ToString() const {
  return fxl::Substitute("$0#$1", fxl::JoinStrings(realm_path, "#"), url);
}

bool operator<(const Moniker& l, const Moniker& r) {
  if (l.realm_path == r.realm_path) {
    return l.url < r.url;
  }
  return l.realm_path < r.realm_path;
}

bool operator==(const Moniker& l, const Moniker& r) {
  return l.url.compare(r.url) == 0 && l.realm_path == r.realm_path;
}

}  // namespace component
