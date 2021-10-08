// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

/// A function that sets the exit code of the process when called. When the
/// current Dart process ends, it will report this exit code to the operating
/// system.
typedef ExitCodeSetter = Function(int exitCode);

/// A default implementation of [ExitCodeSetter].
///
/// Calling this function doesn't prevent a different exit code from being used.
/// Since only a single exit code can be reported by a process, the requested
/// exit code can be replaced by overwriting [exitCode], calling [exit], or
/// calling [setExitCode] again.
void setExitCode(int newExitCode) {
  exitCode = newExitCode;
}
