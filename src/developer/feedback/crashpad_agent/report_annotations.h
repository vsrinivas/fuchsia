// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_REPORT_ANNOTATIONS_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_REPORT_ANNOTATIONS_H_

#include <fuchsia/feedback/cpp/fidl.h>

namespace feedback {

// Builds the final set of annotations to attach to the crash report.
//
// * Most annotations are shared across all crash reports, e.g., |feedback_data|.annotations().
// * Some annotations are report-specific, e.g., Dart exception type.
// * Adds any annotations in the GenericCrashReport from |report|.
std::map<std::string, std::string> BuildAnnotations(const fuchsia::feedback::CrashReport& report,
                                                    const fuchsia::feedback::Data& feedback_data,
                                                    bool has_minidump);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_REPORT_ANNOTATIONS_H_
