// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/pkg_url/fuchsia_pkg_url.h"

#include <string>

#include <re2/re2.h>

#include "src/lib/fxl/strings/concatenate.h"
#include "src/lib/fxl/strings/substitute.h"

namespace component {

static const std::string kFuchsiaPkgPrefix = "fuchsia-pkg://";

// kFuchsiaPkgRexp has the following group matches:
// 1: user/domain/port/etc (everything after scheme, before path)
// 2: package name
// 3: package variant
// 4: package merkle-root hash
// 5: resource path
static const re2::RE2* const kFuchsiaPkgRexp = new re2::RE2(
    "^fuchsia-pkg://([^/]+)/([^/#?]+)(?:/([^/"
    "#?]+))?(?:\\?hash=([^&#]+))?(?:#(.+))?$");

// static
bool FuchsiaPkgUrl::IsFuchsiaPkgScheme(const std::string& url) {
  return url.compare(0, kFuchsiaPkgPrefix.length(), kFuchsiaPkgPrefix) == 0;
}

std::string FuchsiaPkgUrl::GetDefaultComponentCmxPath() const {
  return fxl::Substitute("meta/$0.cmx", package_name());
}

bool FuchsiaPkgUrl::Parse(const std::string& url) {
  package_name_.clear();
  resource_path_.clear();

  if (!re2::RE2::FullMatch(url, *kFuchsiaPkgRexp, &host_name_, &package_name_, &variant_, &hash_,
                           &resource_path_)) {
    return false;
  }

  url_ = url;

  if (variant_.empty()) {
    // TODO(fxbug.dev/4002): Currently this defaults to "0" if not present, but variant
    // will eventually be required in fuchsia-pkg URLs.
    variant_ = "0";
  }

  return true;
}

bool FuchsiaPkgUrl::operator==(const FuchsiaPkgUrl& rhs) const {
  return (this->host_name() == rhs.host_name() && this->package_name() == rhs.package_name() &&
          this->variant() == rhs.variant() && this->resource_path() == rhs.resource_path() &&
          this->hash() == rhs.hash());
}

std::string FuchsiaPkgUrl::pkgfs_dir_path() const {
  return fxl::Substitute("/pkgfs/packages/$0/$1", package_name_, variant_);
}

std::string FuchsiaPkgUrl::package_path() const {
  std::string query = "";
  if (!hash_.empty()) {
    query = fxl::Substitute("?hash=$0", hash_);
  }

  return fxl::Substitute("fuchsia-pkg://$0/$1/$2$3", host_name_, package_name_, variant_, query);
}

const std::string& FuchsiaPkgUrl::ToString() const { return url_; }

}  // namespace component
