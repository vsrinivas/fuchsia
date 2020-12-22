// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxutils/fxutils.dart';
import 'package:test/test.dart';

void main() {
  group('getBuildTarget', () {
    test('should extract correct build target', () {
      expect(
          getBuildTarget(
              '//src/dir:name-with-dash_and_underscore(//build/toolchain/fuchsia:x64)'),
          'src/dir:name-with-dash_and_underscore');
    });

    test('should return null if cannot match', () {
      expect(getBuildTarget('//src/dir:no_toolchain'), null);
      expect(
          getBuildTarget(
              'src/dir:no_doubleslash(//build/toolchain/fuchsia:x64)'),
          null);
    });
  });
}
