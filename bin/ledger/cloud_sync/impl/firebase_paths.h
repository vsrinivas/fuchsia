// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/strings/string_view.h"

namespace cloud_sync {

std::string GetFirebasePathForPage(ftl::StringView user_prefix,
                                   ftl::StringView app_id,
                                   ftl::StringView page_id);

}  // namespace cloud_sync
