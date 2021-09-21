// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fidl_fidl_test_unionmemberremove/fidl_async.dart' as fidllib;

// [START contents]
fidllib.JsonValue writer(String s) {
  final asInt = int.tryParse(s);
  if (asInt != null) {
    return fidllib.JsonValue.withIntValue(asInt);
  }
  return fidllib.JsonValue.withStringValue(s);
}

String reader(fidllib.JsonValue value) {
  switch (value.$tag) {
    case fidllib.JsonValueTag.intValue:
      return '${value.intValue}';
    case fidllib.JsonValueTag.stringValue:
      return value.stringValue;
    case fidllib.JsonValueTag.$unknown:
      return '<${value.$unknownData.data.length} unknown bytes>';
    default:
      return '<unknown new member>';
  }
}

// [END contents]
