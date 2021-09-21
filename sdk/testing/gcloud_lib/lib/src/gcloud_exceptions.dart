// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:googleapis/vision/v1.dart' as vision;

class VisionException implements Exception {
  final vision.Status error;
  VisionException(this.error);

  @override
  String toString() => 'Error from Cloud Vision API: ${error.toJson()}';
}
