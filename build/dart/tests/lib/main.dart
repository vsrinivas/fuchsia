// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io';

import 'package:fuchsia/fuchsia.dart' as fuchsia;

void main(List<String> args) {
  // Verify that resource loading works
  final txt = File('/pkg/data/text_file.txt').readAsStringSync();
  print(txt);
  fuchsia.exit(0);
}
