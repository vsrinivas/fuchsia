// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_PKG_URL_FUCHSIA_PKG_URL_H_
#define LIB_PKG_URL_FUCHSIA_PKG_URL_H_

#include <string>

#include "lib/fxl/macros.h"

namespace component {

class FuchsiaPkgUrl {
 public:
  FuchsiaPkgUrl() = default;
  FuchsiaPkgUrl(FuchsiaPkgUrl&) = default;
  FuchsiaPkgUrl& operator=(FuchsiaPkgUrl&) = default;
  FuchsiaPkgUrl(FuchsiaPkgUrl&&) = default;
  FuchsiaPkgUrl& operator=(FuchsiaPkgUrl&&) = default;

  static bool IsFuchsiaPkgScheme(const std::string& url);

  // Returns the default component's .cmx path of this package, i.e.
  // meta/<package_name>.cmx
  std::string GetDefaultComponentCmxPath() const;

  // Returns the default component's name.
  std::string GetDefaultComponentName() const;

  bool Parse(const std::string& url);

  const std::string& package_name() const { return package_name_; }
  const std::string& resource_path() const { return resource_path_; }
  std::string pkgfs_dir_path() const;

  const std::string& ToString() const;

 private:
  std::string url_;
  std::string package_name_;
  std::string resource_path_;
};

}  // namespace component

#endif  // LIB_PKG_URL_FUCHSIA_PKG_URL_H_
