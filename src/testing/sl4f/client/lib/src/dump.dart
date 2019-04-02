// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io' show Directory, File, FileSystemException, Platform;

/// Supports dumping a trace of data received in various facades as
/// timestamped files into a directory in the filesystem.
class Dump {
  /// Environment variable for the directory to dump images in. If this var is
  /// present, screenshots can optionally be dumped in there.
  static const _dumpDirectoryEnvVar = 'FUCHSIA_TEST_OUTDIR';

  /// Directory to dump screenshots taken. This may be null if it's neither
  /// passed nor set in the environment, in which case no dumps are written.
  final String _dumpDirectory;

  Dump([String dumpDirectory]) : _dumpDirectory =
      dumpDirectory ?? Platform.environment[_dumpDirectoryEnvVar] {
    // Has to be sync because this is a constructor.
    if (_dumpDirectory != null && !Directory(_dumpDirectory).existsSync()) {
      throw FileSystemException('Not found or not a directory', dumpDirectory);
    }
  }

  /// Writes the bytes to the dump directory under a timestamp, the
  /// given topic name and the given file type suffix. Does nothing if
  /// no dump directory is configured.
  void writeAsBytes(String name, String suffix, List<int> bytes) {
    if (_dumpDirectory != null) {
      final filename = '${DateTime.now().toIso8601String()}-$name.$suffix';

      // Write to the file asynchronously so the test can continue.
      // ignore: unawaited_futures
      File([_dumpDirectory, filename].join('/')).writeAsBytes(bytes);
    }
  }
}
