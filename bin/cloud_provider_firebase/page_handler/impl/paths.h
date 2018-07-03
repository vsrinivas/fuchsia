// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_IMPL_PATHS_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_IMPL_PATHS_H_

#include <string>

#include <lib/fxl/strings/string_view.h>

namespace cloud_provider_firebase {

// Returns the common object name prefix used for all objects stored on behalf
// of the given user and app.
std::string GetGcsPrefixForApp(fxl::StringView user_id, fxl::StringView app_id);

// Returns the common object name prefix used for all objects stored for the
// given page, based on the prefix for the app.
std::string GetGcsPrefixForPage(fxl::StringView app_path,
                                fxl::StringView page_id);

// Returns the Firebase path under which the data for the given user is stored.
std::string GetFirebasePathForUser(fxl::StringView user_id);

// Returns the Firebase path under which the data for the given app is stored.
std::string GetFirebasePathForApp(fxl::StringView user_id,
                                  fxl::StringView app_id);

// Returns the Firebase path under which the data for the given page is stored,
// given the path for the app.
std::string GetFirebasePathForPage(fxl::StringView app_path,
                                   fxl::StringView page_id);

}  // namespace cloud_provider_firebase

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_IMPL_PATHS_H_
