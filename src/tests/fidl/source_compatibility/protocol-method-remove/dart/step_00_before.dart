// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fidl_fidl_test_protocolmethodremove/fidl_async.dart' as fidllib;

// [START contents]
class Server extends fidllib.Example {
  @override
  Future<void> existingMethod() async {}

  @override
  Future<void> oldMethod() async {}
}

void client(fidllib.ExampleProxy client) async {
  await client.existingMethod();
  await client.oldMethod();
}
// [END contents]
