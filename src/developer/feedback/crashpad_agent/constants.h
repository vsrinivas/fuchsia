// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CONSTANTS_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CONSTANTS_H_

namespace feedback {

constexpr char kInspectConfigName[] = "config";
constexpr char kInspectReportsName[] = "reports";

constexpr char kCrashpadDatabaseKey[] = "crashpad_database";
constexpr char kCrashpadDatabasePathKey[] = "path";
constexpr char kCrashpadDatabaseMaxSizeInKbKey[] = "max_size_in_kb";
constexpr char kCrashServerKey[] = "crash_server";
constexpr char kCrashServerUploadPolicyKey[] = "upload_policy";
constexpr char kCrashServerUrlKey[] = "url";
constexpr char kFeedbackDataCollectionTimeoutInMillisecondsKey[] =
    "feedback_data_collection_timeout_in_milliseconds";
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CONSTANTS_H_
