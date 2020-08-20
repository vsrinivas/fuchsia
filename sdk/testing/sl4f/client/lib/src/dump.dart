// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

/// Dumps data received in various facades as timestamped files into a directory
/// in the host filesystem.
///
/// This is used by various facade abstractions to dump information received
/// from the Device Under Test (for example, screenshots from [Scenic]). I can
/// also be used by the test to write arbitrary artifacts from the test.
class Dump {
  /// Environment variable for the directory to dump images in.
  ///
  /// If this var is present, data can optionally be dumped in there.
  static const _dumpDirectoryEnvVar = 'FUCHSIA_TEST_OUTDIR';

  /// Directory to dump screenshots taken.
  ///
  /// This may be null if it's neither passed nor set in the environment, in
  /// which case no dumps are written.
  final String _dumpDirectory;

  /// Construct a Dump object which writes files to [dumpDirectory].
  ///
  /// Not supplying a [dumpDirectory] parameter, or supplying null, or an empty
  /// string, means the dump directory specification is taken from the
  /// environment. If that's not specified, or is specified to be the empty
  /// string, dump is disabled.
  ///
  /// If dump is not disabled, then the dump directory specification
  /// must be valid in that it designates a directory by an absolute path name
  /// that must either already exist, or be amenable to be created by this
  /// constructor.
  ///
  /// The [dumpDirectory] must be an absolute path. A relative path is
  /// ambiguous, because it's not clear relative to what. It would normally be
  /// the current working directory, but other relative paths in this library
  /// are resolved relative to the location of the binary.
  Dump([String dumpDirectory])
      : _dumpDirectory = _notEmptyString(dumpDirectory)
            ? dumpDirectory
            : Platform.environment[_dumpDirectoryEnvVar] {
    if (hasDumpDirectory) {
      // See explanation above. Relative path would be ambiguous.
      if (!_dumpDirectory.startsWith('/')) {
        throw ArgumentError.value(_dumpDirectory, 'Must be absolute path');
      }

      // Has to use sync operations because this is a constructor.
      final directory = Directory(_dumpDirectory);
      if (!directory.existsSync()) {
        // Try to create the directory. This will throw if it fails to create
        // the directory.
        directory.createSync(recursive: true);
      }
    }
  }

  /// Writes the bytes to the dump directory under a timestamp, the
  /// given topic name and the given file type suffix. Does nothing if
  /// no dump directory is configured.
  ///
  /// Returns the [File] object of the newly created file.
  Future<File> writeAsBytes(String name, String suffix, List<int> bytes) =>
      createFile(name, suffix)?.writeAsBytes(bytes);

  /// Writes the string to the dump directory under a timestamp, the
  /// given topic name and the given file type suffix. Does nothing if
  /// no dump directory is configured.
  ///
  /// Returns the [File] object of the newly created file.
  Future<File> writeAsString(String name, String suffix, String contents) =>
      createFile(name, suffix)?.writeAsString(contents);

  /// Opens the appropriate file for writing.
  ///
  /// Returns the [IOSink] object of the newly created file for writing.
  IOSink openForWrite(String name, String suffix) =>
      createFile(name, suffix)?.openWrite();

  /// Creates a file in the dump directory.
  ///
  /// Returns null if dump directory is invalid.
  File createFile(String name, String suffix) {
    if (!hasDumpDirectory) {
      return null;
    }

    final filename = '${DateTime.now().toIso8601String()}-$name.$suffix';

    return File([_dumpDirectory, filename].join('/'));
  }

  /// Whether the dump directory is valid and dump files will be written by this
  /// object.
  bool get hasDumpDirectory => _notEmptyString(_dumpDirectory);

  static bool _notEmptyString(final String value) =>
      value != null && value.isNotEmpty;
}
