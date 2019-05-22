// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_BUGREPORT_BUG_REPORT_SCHEMA_H_
#define SRC_DEVELOPER_BUGREPORT_BUG_REPORT_SCHEMA_H_

namespace fuchsia {
namespace bugreport {

constexpr char kBugReportJsonSchema[] = R"({
  "type": "object",
  "properties": {
    "annotations": {
      "type": "object"
    },
    "attachments": {
      "type": "object"
    }
  },
  "required": [
    "annotations",
    "attachments"
  ],
  "additionalProperties": false
})";

}  // namespace bugreport
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_BUGREPORT_BUG_REPORT_SCHEMA_H_
