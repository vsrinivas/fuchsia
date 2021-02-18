// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'package:fuchsia_inspect/src/inspect/internal/_inspect_impl.dart';
import 'package:fuchsia_inspect/src/vmo/vmo_writer.dart';
import 'package:fuchsia_inspect/testing.dart';
import 'package:test/test.dart';

void main() {
  test('Inspect root node is non-null by default', () {
    var vmo = FakeVmoHolder(512);
    var writer = VmoWriter.withVmo(vmo);

    var inspect = InspectImpl(writer);
    expect(inspect.root, isNotNull);
  });
}
