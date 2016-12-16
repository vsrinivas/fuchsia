// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/strings/string_view.h"

namespace cloud_sync {

// Returns the Firebase path under which the data for the given app is stored.
std::string GetFirebasePathForApp(ftl::StringView user_prefix,
                                  ftl::StringView app_id);

// Returns the Firebase path under which the data for the given page is stored,
// given the path for the app.
std::string GetFirebasePathForPage(ftl::StringView app_path,
                                   ftl::StringView page_id);

}  // namespace cloud_sync
