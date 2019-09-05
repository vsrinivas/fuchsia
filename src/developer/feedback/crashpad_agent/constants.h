// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CONSTANTS_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CONSTANTS_H_

namespace fuchsia {
namespace crash {

constexpr char kInspectConfigName[] = "config";
constexpr char kInspectReportsName[] = "reports";
const char kCrashpadDatabaseKey[] = "crashpad_database";
const char kCrashpadDatabasePathKey[] = "path";
const char kCrashpadDatabaseMaxSizeInKbKey[] = "max_size_in_kb";
const char kCrashServerKey[] = "crash_server";
const char kCrashServerEnableUploadKey[] = "enable_upload";
const char kCrashServerUrlKey[] = "url";
const char kFeedbackDataCollectionTimeoutInSecondsKey[] =
        "feedback_data_collection_timeout_in_milliseconds";

} // namespace crash
} // namespace fuchsia

#endif // #SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CONSTANTS_H_
