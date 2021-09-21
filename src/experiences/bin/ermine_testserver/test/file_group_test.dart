// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// ignore_for_file: implementation_imports
import 'package:ermine_testserver/src/file_group.dart';
import 'package:test/test.dart';

void main() {
  test('FileGroup should return a right path', () async {
    final fileGroup = FileGroup(
      'group',
      ['file1.txt', 'file2.html', 'file3.css'],
    );

    expect(fileGroup.getPath(0), '/pkg/data/group/file1.txt');
    expect(fileGroup.getPath(1), '/pkg/data/group/file2.html');
    expect(fileGroup.getPath(2), '/pkg/data/group/file3.css');
  });
}
