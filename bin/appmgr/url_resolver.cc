// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/src/manager/url_resolver.h"

namespace app {
namespace {

constexpr char kFileUriPrefix[] = "file://";
constexpr size_t kFileUriPrefixLength = sizeof(kFileUriPrefix) - 1;

}  // namespace

std::string CanonicalizeURL(const std::string& url) {
  if (!url.empty() && url.find(":") == std::string::npos)
    return kFileUriPrefix + url;
  return url;
}

std::string GetPathFromURL(const std::string& url) {
  if (url.find(kFileUriPrefix) == 0)
    return url.substr(kFileUriPrefixLength);
  return std::string();
}

std::string GetURLFromPath(const std::string& path) {
  return kFileUriPrefix + path;
}

}  // namespace app
