// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_BUGREPORT_COMMAND_LINE_OPTIONS_H_
#define SRC_DEVELOPER_FORENSICS_BUGREPORT_COMMAND_LINE_OPTIONS_H_

namespace forensics {
namespace bugreport {

enum class Mode {
  FAILURE,
  HELP,
  DEFAULT,
};

Mode ParseModeFromArgcArgv(int argc, const char* const* argv);

}  // namespace bugreport
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_BUGREPORT_COMMAND_LINE_OPTIONS_H_
