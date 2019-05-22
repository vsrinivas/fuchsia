// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_BUGREPORT_BUG_REPORTER_H_
#define SRC_DEVELOPER_BUGREPORT_BUG_REPORTER_H_

#include <lib/sys/cpp/service_directory.h>

#include <memory>

namespace fuchsia {
namespace bugreport {

// Makes a JSON file representing a bug report containing all the feedback data
// by connecting to fuchsia.feedback.DataProvider from |services|.
//
// By default, the JSON file is streamed to stdout. Use |out_filename| to output
// it to a file.
bool MakeBugReport(std::shared_ptr<::sys::ServiceDirectory> services,
                   const char* out_filename = nullptr);

}  // namespace bugreport
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_BUGREPORT_BUG_REPORTER_H_
