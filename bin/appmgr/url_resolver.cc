// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/url_resolver.h"

#include "lib/fxl/strings/ascii.h"

namespace component {
namespace {

constexpr char kFileUriPrefix[] = "file://";
constexpr size_t kFileUriPrefixLength = sizeof(kFileUriPrefix) - 1;

}  // namespace

std::string CanonicalizeURL(const std::string& url) {
  if (!url.empty() && url.find(':') == std::string::npos)
    return kFileUriPrefix + url;
  return url;
}

std::string GetSchemeFromURL(const std::string& url) {
  size_t len = url.find(':');
  if (len == std::string::npos)
    return std::string();
  std::string result(len, 0);
  for (size_t i = 0; i < len; ++i)
    result[i] = fxl::ToLowerASCII(url[i]);
  return result;
}

std::string GetPathFromURL(const std::string& url) {
  if (url.find(kFileUriPrefix) == 0)
    return url.substr(kFileUriPrefixLength);
  return std::string();
}

std::string GetURLFromPath(const std::string& path) {
  return kFileUriPrefix + path;
}

}  // namespace component
