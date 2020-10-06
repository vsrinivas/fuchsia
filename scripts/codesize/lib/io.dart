// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

/// Library for injecting and mocking I/O functionalities.
library io;

import 'dart:async';
import 'dart:io' as dart_io;
import 'dart:mirrors';

import 'package:fxutils/fxutils.dart' as fxutils;
import 'package:file/file.dart';
import 'package:file/local.dart';
import 'package:process/process.dart';

/// Abstract interface for doing I/O (console output, process creation, etc.).
/// See `Standard` for the production implemention used to run `fx codesize`.
/// When running unit tests, we would provide mock implementations which
/// record I/O activities instead.
abstract class Io {
  static Io get() =>
      Zone.current[#io] ?? (throw Exception('Missing dependency #io'));

  /// The standard program output stream.
  StringSink get out;

  /// The error output stream.
  StringSink get err;

  /// Prints the object to the standard output stream.
  void print(Object object);

  ProcessManager get processManager;

  fxutils.FxEnv get fxEnv;

  fxutils.Fx get fx;

  /// Sets the exit code of the program.
  set exitCode(int code);

  /// Returns the current working directory path.
  String get cwd;

  /// Returns the environment variables;
  Map<String, String> get environment;

  File createTempFile(String name);
}

class Standard implements Io {
  static Map<Object, Object> inject() => {#io: Standard()};

  final FileSystem _fs = LocalFileSystem();

  @override
  StringSink get out => dart_io.stdout;

  @override
  StringSink get err => dart_io.stderr;

  @override
  void print(Object object) {
    dart_io.stdout.writeln(object);
  }

  @override
  ProcessManager get processManager => LocalProcessManager();

  fxutils.FxEnv _fxEnv;

  @override
  fxutils.FxEnv get fxEnv {
    if (_fxEnv != null) {
      return _fxEnv;
    }

    final envReader = fxutils.EnvReader(cwd: cwd, environment: environment);
    return _fxEnv = fxutils.FxEnv(envReader: envReader);
  }

  fxutils.Fx _fx;

  @override
  fxutils.Fx get fx {
    if (_fx != null) {
      return _fx;
    }

    return _fx = fxutils.Fx(fxEnv: fxEnv);
  }

  @override
  set exitCode(int code) => dart_io.exitCode = code;

  @override
  String get cwd => dart_io.Directory.current.path;

  @override
  Map<String, String> get environment => dart_io.Platform.environment;

  @override
  File createTempFile(String name) =>
      _fs.systemTempDirectory.createTempSync().childFile(name);
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
