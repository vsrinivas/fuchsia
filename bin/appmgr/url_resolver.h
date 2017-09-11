// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_SRC_MANAGER_URL_RESOLVER_H_
#define APPLICATION_SRC_MANAGER_URL_RESOLVER_H_

#include <string>

namespace app {

// Canonicalizes a URL, if possible.  Otherwise, returns an empty string/
std::string CanonicalizeURL(const std::string& url);

// Resolves a URL into a path, if possible. Otherwise, returns an empty string.
std::string GetPathFromURL(const std::string& url);

// Returns a file:// URL for the given path.
std::string GetURLFromPath(const std::string& path);

}  // namespace app

#endif  // APPLICATION_SRC_MANAGER_URL_RESOLVER_H_
