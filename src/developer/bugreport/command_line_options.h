// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_BUGREPORT_COMMAND_LINE_OPTIONS_H_
#define SRC_DEVELOPER_BUGREPORT_COMMAND_LINE_OPTIONS_H_

namespace fuchsia {
namespace bugreport {

enum class Mode {
  FAILURE,
  HELP,
  DEFAULT,
  MINIMAL,
};

Mode ParseModeFromArgcArgv(int argc, const char* const* argv);

}  // namespace bugreport
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_BUGREPORT_COMMAND_LINE_OPTIONS_H_
