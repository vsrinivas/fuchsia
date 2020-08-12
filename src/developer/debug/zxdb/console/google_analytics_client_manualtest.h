// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_GOOGLE_ANALYTICS_CLIENT_MANUALTEST_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_GOOGLE_ANALYTICS_CLIENT_MANUALTEST_H_

#include <lib/fit/result.h>

#include "src/developer/debug/zxdb/console/google_analytics_client.h"

namespace zxdb {

int ProcessAddEventResult(const fit::result<void, GoogleAnalyticsNetError>& result);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_GOOGLE_ANALYTICS_CLIENT_MANUALTEST_H_
