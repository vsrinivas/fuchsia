// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io';

import 'package:glob/glob.dart';
import 'package:glob/list_local_fs.dart';
// ignore: avoid_as
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

void main() {
  sl4f.Dump dump;
  Directory dumpDir;

  group(sl4f.Dump, () {
    setUp(() {
      dumpDir = Directory.systemTemp.createTempSync('dump-test');
      dump = sl4f.Dump(dumpDir.path);
    });

    tearDown(() {
      dumpDir.deleteSync(recursive: true);
    });

    test('writeAsBytes creates expected file', () async {
      File file = await dump.writeAsBytes(
          'write-as-bytes', 'tmp', [0x68, 0x65, 0x6c, 0x6c, 0x6f]);

      expect(file.path, startsWith(dumpDir.path));
      expect(
          file.path,
          matches(RegExp(r'.*\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{1,6}'
              r'-write-as-bytes.tmp$')));
      expect(FileStat.statSync(file.path).size, greaterThan(0));
      expect(file.readAsStringSync(), 'hello');
    });
    test('openForWrite creates expected file', () async {
      IOSink sink = dump.openForWrite('open-for-write', 'tmp');

      // Can't do cascaded invocations together with await, silly rabbit.
      // ignore: cascade_invocations
      sink.write('hello');
      await sink.flush();
      await sink.close();

      final File file = Glob('*-open-for-write.tmp')
          .listSync(root: dumpDir.path)
          .single as File; // ignore: avoid_as
      expect(file.path, startsWith(dumpDir.path));
      expect(
          file.path,
          matches(RegExp(r'.*\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{1,6}'
              r'-open-for-write.tmp$')));
      expect(FileStat.statSync(file.path).size, greaterThan(0));
      expect(file.readAsStringSync(), 'hello');
    });
  });

  test('constructor creates directory', () {
    dumpDir = Directory.systemTemp.createTempSync('dump-test');
    dump = sl4f.Dump('${dumpDir.path}/foo/bar');
    addTearDown(() => dumpDir.deleteSync(recursive: true));

    expect(Directory('${dumpDir.path}/foo/bar').existsSync(), isTrue);
  });
}
