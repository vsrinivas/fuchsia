// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

class TestRunner {
  static final TestRunner runner = TestRunner();

  Future<ProcessResult> run(
    String command,
    List<String> args, {
    String workingDirectory,
  }) async {
    return Process.run(command, args, workingDirectory: workingDirectory);
  }
}
