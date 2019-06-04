// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

import 'package:sl4f/sl4f.dart';

class MockSl4f extends Mock implements Sl4f {}

class MockFile extends Mock implements File {}

void main(List<String> args) {
  test('put file base64s and calls sl4f', () async {
    final sl4f = MockSl4f();
    final file = MockFile();
    when(file.readAsBytes()).thenAnswer((_) => Future.value(
        [0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x77, 0x6f, 0x72, 0x6c, 0x64]));
    when(sl4f.request('file_facade.WriteFile'))
        .thenAnswer((_) => Future.value('Success'));

    await Storage(sl4f).putFile('/a/path', file);

    verify(sl4f.request('file_facade.WriteFile',
        {'dst': '/a/path', 'data': 'aGVsbG93b3JsZA=='}));
  });
}
