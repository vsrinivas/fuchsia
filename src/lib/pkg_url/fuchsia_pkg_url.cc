// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/pkg_url/fuchsia_pkg_url.h"

#include <regex>
#include <string>

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
static const std::regex* const kFuchsiaPkgRexp = new std::regex(
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

  std::smatch match_data;

  if (!std::regex_match(url, match_data, *kFuchsiaPkgRexp)) {
    return false;
  }

  url_ = match_data[0].str();

  host_name_ = match_data[1].str();
  package_name_ = match_data[2].str();
  variant_ = match_data[3].str();
  if (variant_.empty()) {
    // TODO(fxbug.dev/4002): Currently this defaults to "0" if not present, but variant
    // will eventually be required in fuchsia-pkg URLs.
    variant_ = "0";
  }
  hash_ = match_data[4].str();
  resource_path_ = match_data[5].str();

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
