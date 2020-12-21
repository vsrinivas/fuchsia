// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fidl_test_protocolmethodremove/fidl_async.dart' as fidllib;

// [START contents]
class Server extends fidllib.Example {
  @override
  Future<void> existingMethod() async {}
}

void client(fidllib.ExampleProxy client) async {
  await client.existingMethod();
}
// [END contents]
