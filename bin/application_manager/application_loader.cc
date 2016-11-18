// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/application_manager/application_loader.h"

#include <fcntl.h>

#include <utility>

#include "apps/modular/src/application_manager/url_resolver.h"

namespace modular {

ApplicationLoader::ApplicationLoader(std::vector<std::string> path)
    : path_(std::move(path)) {}

ApplicationLoader::~ApplicationLoader() {}

std::tuple<ftl::UniqueFD, std::string> ApplicationLoader::Open(
    const std::string& url) {
  std::string path = GetPathFromURL(url);
  if (path.empty()) {
    // TODO(abarth): Support URL schemes other than file:// by querying the host
    // for an application runner.
    FTL_LOG(ERROR) << "Cannot load " << url
                   << " because the scheme is not supported.";
  } else {
    ftl::UniqueFD fd(open(path.c_str(), O_RDONLY));
    if (!fd.is_valid() && path[0] != '/') {
      for (const auto& entry : path_) {
        std::string qualified_path = entry + "/" + path;
        fd.reset(open(qualified_path.c_str(), O_RDONLY));
        if (fd.is_valid()) {
          path = qualified_path;
          break;
        }
      }
    }
    if (fd.is_valid())
      return std::make_tuple(std::move(fd), std::move(path));
  }

  return std::make_tuple(ftl::UniqueFD(), std::string());
}

}  // namespace modular
