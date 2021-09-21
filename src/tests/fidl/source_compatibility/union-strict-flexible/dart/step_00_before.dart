// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fidl_fidl_test_unionstrictflexible/fidl_async.dart' as fidllib;

// [START contents]
void useUnion(fidllib.JsonValue value) {
  assert(value.$unknownData == null);
  switch (value.$tag) {
    case fidllib.JsonValueTag.intValue:
      print('int value: ${value.intValue}');
      break;
    case fidllib.JsonValueTag.stringValue:
      print('string value: ${value.stringValue}');
      break;
  }
}

// [END contents]
