// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:test/test.dart';
import 'package:codesize/symbols/cache.dart';

void main() {
  group('Cache', () {
    test('pathForBuildId', () {
      expect(Cache(Directory('.build-id')).pathForBuildId('123456').path,
          equals('.build-id/12/3456.debug'));
    });

    test('pathForTempBuildId', () {
      expect(Cache(Directory('.build-id')).pathForTempBuildId('123456').path,
          equals('.build-id/123456.part'));
    });
  });
}
