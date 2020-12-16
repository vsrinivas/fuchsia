// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fidl_test_addmethod/fidl_async.dart' as fidl;

class ExampleImpl extends fidl.Example {
  @override
  Future<void> existingMethod() async {}
  @override
  Future<void> newMethod() async {}
}

Future<void> invokeMethods(fidl.ExampleProxy proxy) async {
  await proxy.existingMethod();
  await proxy.newMethod();
}
