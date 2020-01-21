// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:glob/glob.dart';
import 'package:test/test.dart';
// ignore: avoid_as
import 'package:sl4f/sl4f.dart' as sl4f;

void main() {
  sl4f.Dump dump;
  Directory dumpDir;

  setUp(() {
    dumpDir = Directory.systemTemp.createTempSync('dump-test');
    dump = sl4f.Dump(dumpDir.path);
  });

  tearDown(() {
    dumpDir.deleteSync(recursive: true);
  });

  group(sl4f.Dump, () {
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

      // This is the official way of converting a FileSystemEntity to a File.
      // ignore: avoid_as
      final file = Glob('*-open-for-write.tmp')
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
}
