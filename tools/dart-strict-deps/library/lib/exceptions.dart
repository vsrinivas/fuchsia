// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class StrictDepsFatalException implements Exception {
  final String message;

  StrictDepsFatalException(this.message);

  @override
  String toString() => message;
}

class StrictDepsInvalidInputFileException implements Exception {
  final String message;

  StrictDepsInvalidInputFileException(this.message);

  @override
  String toString() => message;
}
