// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fidl_test_addmethod/fidl_async.dart' as fidllib;

class Impl extends fidllib.ExampleProtocol {
  @override
  Future<void> existingMethod() async {}

  @override
  Future<void> newMethod() async {}
}
