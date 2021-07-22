// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Any exception when dealing with the Ffx wrapper.
class FfxException implements Exception {
  dynamic error;
  FfxException(this.error);

  @override
  String toString() => 'Error when handling ffx: $error';
}
