// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_REPORT_ANNOTATIONS_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_REPORT_ANNOTATIONS_H_

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl.h>

#include <map>
#include <string>

namespace fuchsia {
namespace crash {

// Returns the default annotations we want in all crash reports.
//
// |feedback_data| may contain annotations that are shared with other feedback reports, e.g., user
// feedback reports.
//
// Most annotations are shared between userspace and kernel crashes. Add additional arguments to
// this function for values that differ between the two, e.g., the program name can be extracted
// from the crashing process in userspace, but it's just "kernel" in kernel space.
//
// TODO(DX-1820): delete once transitioned to fuchsia.feedback.CrashReporter.
std::map<std::string, std::string> MakeDefaultAnnotations(
    const fuchsia::feedback::Data& feedback_data, const std::string& program_name);

// Returns the annotations we want in a crash report for managed runtime exceptions.
//
// May augment the default annotation map from MakeDefaultAnnotations() or simply return the
// default.
//
// TODO(DX-1820): delete once transitioned to fuchsia.feedback.CrashReporter.
std::map<std::string, std::string> MakeManagedRuntimeExceptionAnnotations(
    const fuchsia::feedback::Data& feedback_data, const std::string& component_url,
    ManagedRuntimeException* exception);

// Builds the final set of annotations to attach to the crash report.
//
// * Most annotations are shared across all crash reports, e.g., |feedback_data|.annotations().
// * Some annotations are report-specific, e.g., Dart exception type.
// * Adds any annotations in the GenericCrashReport from |report|.
std::map<std::string, std::string> BuildAnnotations(const fuchsia::feedback::CrashReport& report,
                                                    const fuchsia::feedback::Data& feedback_data);

}  // namespace crash
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_REPORT_ANNOTATIONS_H_
