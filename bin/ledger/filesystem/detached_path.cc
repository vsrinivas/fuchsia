// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/filesystem/detached_path.h"

#include <lib/fxl/strings/concatenate.h>

#include <utility>

namespace ledger {

DetachedPath::DetachedPath(int root_fd, std::string path)
    : root_fd_(root_fd), path_(std::move(path)) {}

DetachedPath::~DetachedPath() {}

DetachedPath::DetachedPath(const DetachedPath& other) = default;

DetachedPath::DetachedPath(DetachedPath&& other) noexcept = default;

DetachedPath& DetachedPath::operator=(const DetachedPath& other) = default;

DetachedPath& DetachedPath::operator=(DetachedPath&&) noexcept = default;

DetachedPath DetachedPath::SubPath(fxl::StringView path) const {
  return DetachedPath(root_fd_, fxl::Concatenate({path_, "/", path}));
}

DetachedPath DetachedPath::SubPath(
    std::initializer_list<fxl::StringView> components) const {
  std::string end_path = path_;
  for (const auto& component : components) {
    end_path.push_back('/');
    end_path.append(component.data(), component.size());
  }
  return DetachedPath(root_fd_, std::move(end_path));
}

}  // namespace ledger
