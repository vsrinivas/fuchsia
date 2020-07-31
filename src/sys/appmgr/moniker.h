// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_MONIKER_H_
#define SRC_SYS_APPMGR_MONIKER_H_

#include <numeric>
#include <string>
#include <vector>

namespace component {

// Uniquely identifies a component instance by its topological path.
struct Moniker {
  const std::string url;
  const std::vector<std::string> realm_path;

  // Returns a string representation of the moniker.
  std::string ToString() const;
};

// This operator allows |Moniker| to be used as a key in a std::map<>.
bool operator<(const Moniker& l, const Moniker& r);

// This operator allows |Moniker| to be used in an std::find<>
bool operator==(const Moniker& l, const Moniker& r);

}  // namespace component

namespace std {
// This allows |Moniker| to be used in an std::unordered_set<>
template <>
struct hash<component::Moniker> {
  size_t operator()(const component::Moniker& moniker) const {
    string realm_url =
        accumulate(moniker.realm_path.begin(), moniker.realm_path.end(), std::string(""));
    return hash<string>()(moniker.url + ":" + realm_url);
  }
};
}  // namespace std

#endif  // SRC_SYS_APPMGR_MONIKER_H_
