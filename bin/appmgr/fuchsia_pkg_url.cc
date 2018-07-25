// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/fuchsia_pkg_url.h"
#include "lib/fxl/strings/concatenate.h"

#include <regex>
#include <string>

namespace component {

constexpr char kFuchsiaPkgPrefix[] = "fuchsia-pkg://";
// Assume anything between the last / and # is the package name.
// TODO(CP-110): Support pkg-variant and pkg-hash.
static const std::regex* const kPackageName = new std::regex("([^/]+)(?=#)");
// Resource path is anything after #.
static const std::regex* const kResourcePath = new std::regex("([^#]+)$");

bool FuchsiaPkgUrl::IsFuchsiaPkgScheme(const std::string& url) {
  return url.find(kFuchsiaPkgPrefix) == 0;
}

bool FuchsiaPkgUrl::Parse(const std::string& url) {
  package_name_.clear();
  resource_path_.clear();

  if (!IsFuchsiaPkgScheme(url)) {
    return false;
  }

  std::smatch sm;
  if (!(std::regex_search(url, sm, *kPackageName) && sm.size() >= 2)) {
    return false;
  }
  package_name_ = sm[1].str();
  if (!(std::regex_search(url, sm, *kResourcePath) && sm.size() >= 2)) {
    return false;
  }
  resource_path_ = sm[1].str();
  return true;
}

std::string FuchsiaPkgUrl::pkgfs_dir_path() {
  // TODO(CP-105): We're currently hardcoding version 0 of the package,
  // but we'll eventually need to do something smarter.
  return fxl::Concatenate({"/pkgfs/packages/", package_name(), "/0"});
}

std::string FuchsiaPkgUrl::pkgfs_resource_path() {
  return fxl::Concatenate({pkgfs_dir_path(), "/", resource_path()});
}

}  // namespace component
