// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_io/fidl_async.dart';

/// Common utilities for working with flags during open/clone.
class Flags {
  /// Default rights.
  static OpenFlags fsRightsDefault() {
    return OpenFlags.rightReadable | OpenFlags.rightWritable;
  }

  /// Returns true if the OPEN_FLAG_NODE_REFERENCE bit is set in |flags|.
  static bool isNodeReference(OpenFlags flags) {
    return (flags & OpenFlags.nodeReference) != OpenFlags.$none;
  }

  /// Returns true if the CLONE_FLAG_SAME_RIGHTS bit is set in |flags|.
  static bool shouldCloneWithSameRights(OpenFlags flags) {
    return (flags & OpenFlags.cloneSameRights) != OpenFlags.$none;
  }

  /// Returns true if the OPEN_FLAG_POSIX_WRITABLE bit is set in |flags|.
  static bool isPosixWritable(OpenFlags flags) {
    return (flags & OpenFlags.posixWritable) != OpenFlags.$none;
  }

  /// Returns true if the OPEN_FLAG_POSIX_EXECUTABLE bit is set in |flags|.
  static bool isPosixExecutable(OpenFlags flags) {
    return (flags & OpenFlags.posixExecutable) != OpenFlags.$none;
  }

  /// Returns true if the rights flags in |flagsA| does not exceed
  /// those in |flagsB|.
  static bool stricterOrSameRights(OpenFlags flagsA, OpenFlags flagsB) {
    var rightsA = flagsA & openRights;
    var rightsB = flagsB & openRights;
    return (rightsA & ~rightsB) == OpenFlags.$none;
  }

  /// Perform basic flags validation relevant to |Directory.Open| and
  /// |Node.Clone|.
  /// Returns false if the flags combination is invalid.
  static bool inputPrecondition(OpenFlags flags) {
    // Explicitly reject NODE_REFERENCE together with any invalid flags.
    if ((flags & OpenFlags.nodeReference) != OpenFlags.$none) {
      if ((flags & ~openFlagsAllowedWithNodeReference) != OpenFlags.$none) {
        return false;
      }
    }
    return true;
  }
}
