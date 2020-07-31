// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Library for injecting and mocking I/O functionalities.
library io;

import 'dart:async';
import 'dart:io';
import 'dart:mirrors';

import 'package:process/process.dart';

/// Abstract interface for doing I/O (console output, process creation, etc.).
/// See `Standard` for the production implemention used to run `fx codesize`.
/// When running unit tests, we would provide mock implementations which
/// record I/O activities instead.
abstract class Io {
  IOSink get out;
  IOSink get err;
  void print(Object object);

  ProcessManager get processManager;

  static Io get() =>
      Zone.current[#io] ?? (throw Exception('Missing dependency #io'));
}

class Standard implements Io {
  @override
  IOSink get err => stderr;

  @override
  IOSink get out => stdout;

  @override
  void print(Object object) {
    stdout.writeln(object);
  }

  @override
  ProcessManager get processManager => LocalProcessManager();

  static Map<Object, Object> inject() => {#io: Standard()};
}

Future<R> runWithIo<IoType extends Io, R>(Future<R> Function() f) {
  // ignore: avoid_as
  final IoType io = (reflectType(IoType) as ClassMirror)
      .newInstance(Symbol(''), []).reflectee;
  return runZoned(f,
      zoneSpecification: ZoneSpecification(
          print: (Zone self, ZoneDelegate parent, Zone zone, String line) =>
              io.out.writeln(line)),
      zoneValues: {#io: io});
}
