// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_bazel_examples_simple/fidl.dart';

void main(List<String> args) {
  final Hello hello = const Hello(world: 314);
  print('Hello: $hello');
}
