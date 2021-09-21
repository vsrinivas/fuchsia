// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fidl_fidl_test_enumflexiblestrict/fidl_async.dart' as fidllib;

// [START contents]
fidllib.Color complement(fidllib.Color color) {
  if (color.isUnknown()) {
    return color;
  }
  switch (color) {
    case fidllib.Color.blue:
      return fidllib.Color.red;
    case fidllib.Color.red:
      return fidllib.Color.blue;
    default:
      return null;
  }
}

// [END contents]
