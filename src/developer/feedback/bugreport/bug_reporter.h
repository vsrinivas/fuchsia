// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_BUGREPORT_BUG_REPORTER_H_
#define SRC_DEVELOPER_FEEDBACK_BUGREPORT_BUG_REPORTER_H_

#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <set>
#include <string>

namespace fuchsia {
namespace bugreport {

// Makes a JSON file representing a bug report containing the feedback data (annotations and
// attachments) collected from fuchsia.feedback.DataProvider from |services|.
//
// By default:
//   * all the attachments are reported. Use |attachment_allowlist| to restrict the attachments to
//     specific keys, e.g., to minimize the output.
//   * the JSON file is streamed to stdout. Use |out_filename| to output it to a file.
bool MakeBugReport(std::shared_ptr<::sys::ServiceDirectory> services,
                   const std::set<std::string>& attachment_allowlist = {},
                   const char* out_filename = nullptr);

}  // namespace bugreport
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_BUGREPORT_BUG_REPORTER_H_
