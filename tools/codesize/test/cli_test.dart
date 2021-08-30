// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:codesize/cli.dart';
import 'package:test/test.dart';

void main() {
  group('parseQueryConstructorArgs', () {
    test('foo: 5, bar: abc', () {
      expect(
          parseQueryConstructorArgs('foo: 5, bar: abc'),
          equals(<String, String>{
            'foo': '5',
            'bar': 'abc',
          }));
    });

    test('showCompileUnit: true, showProgram: true, hideUnknown: false', () {
      expect(
          parseQueryConstructorArgs(
              'showCompileUnit: true, showProgram: true, hideUnknown: false'),
          equals(<String, String>{
            'showCompileUnit': 'true',
            'showProgram': 'true',
            'hideUnknown': 'false',
          }));
    });
  });
}
