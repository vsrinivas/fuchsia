// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// An [Exception] indicating that the Module is in an
/// invalid state. This exception should only be thrown when
/// the module is in a state which is cannot recover from and
/// will need to crash.
class ModuleStateException implements Exception {
  /// A string indicating the what led to the exception.
  ///
  /// This message should be as descriptive as possible since
  /// this exception should not be caught and will lead to a crash.
  final String message;

  /// The default constructor for this module.
  ModuleStateException(this.message);

  @override
  String toString() => 'ModuleStateException: $message';
}
