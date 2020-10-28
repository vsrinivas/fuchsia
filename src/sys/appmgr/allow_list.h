// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_ALLOW_LIST_H_
#define SRC_SYS_APPMGR_ALLOW_LIST_H_

#include <string>
#include <vector>

#include <fbl/unique_fd.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/pkg_url/fuchsia_pkg_url.h"

namespace component {

// Represents a list of component URLs that are allowed to use a certain feature.
class AllowList {
 public:
  // Parses the given file as an allowlist.
  //
  // The file should consist of bare strings or component URLs, one per line. May contain comments,
  // starting with `#`.
  //
  // Wildcards are supported for matching the resource path (at most one).
  //
  // No validation is done on the format of the file.
  explicit AllowList(const fbl::unique_fd& dir, const std::string& file_path);
  // Returns true if |url| is allowed according to the allowlist. If |url| contains a variant or
  // hash, they are ignored for the purposes of matching.
  bool IsAllowed(const FuchsiaPkgUrl& url) const;

 private:
  static bool IsMatch(const std::string& allowlist_item, const FuchsiaPkgUrl& in_url);

  std::vector<std::string> list_;
  bool allow_all_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AllowList);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_ALLOW_LIST_H_
