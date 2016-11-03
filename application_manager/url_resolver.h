// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_APPLICATION_MANAGER_URL_RESOLVER_H_
#define APPS_MODULAR_APPLICATION_MANAGER_URL_RESOLVER_H_

#include <string>

namespace modular {

// Resolves a URL into a path, if possible. Otherwise, returns an empty string.
std::string GetPathFromURL(const std::string& url);

// Returns a file:// URL for the given path.
std::string GetURLFromPath(const std::string& path);

}  // namespace modular

#endif  // APPS_MODULAR_APPLICATION_MANAGER_URL_RESOLVER_H_
