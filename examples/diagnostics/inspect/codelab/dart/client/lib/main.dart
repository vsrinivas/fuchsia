// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: implementation_imports

import 'dart:io';
import 'package:fidl_fuchsia_component/fidl_async.dart';
import 'package:fidl_fuchsia_component_decl/fidl_async.dart';
import 'package:fidl_fuchsia_examples_inspect/fidl_async.dart';
import 'package:fidl_fuchsia_io/fidl_async.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/src/incoming.dart';

Future<void> main() async {
  setupLogger(name: 'inspect_dart_codelab', globalTags: ['client']);

  // TODO(fxbug.dev/88383): dart doesn't support `program.args` at the moment.
  const args = ['Hello', 'world'];

  final realm = RealmProxy();
  Incoming.fromSvcPath().connectToService(realm);

  final reverserExposedDir = DirectoryProxy();
  await realm.openExposedDir(
      ChildRef(name: 'reverser'), reverserExposedDir.ctrl.request());
  final reverser = ReverserProxy();
  Incoming.withDirectory(reverserExposedDir).connectToService(reverser);

  final fizzbuzzExposedDir = DirectoryProxy();
  await realm.openExposedDir(
      ChildRef(name: 'fizzbuzz'), fizzbuzzExposedDir.ctrl.request());
  final fizzbuzzBinder = BinderProxy();
  Incoming.withDirectory(fizzbuzzExposedDir).connectToService(fizzbuzzBinder);

  // [START reverse_loop]
  for (int i = 0; i < args.length; i++) {
    log.info('Input: ${args[i]}');
    final response = await reverser.reverse(args[i]);
    log.info('Output: $response');
  }
  // [END reverse_loop]

  log.info('Done. Press Ctrl+C to exit');

  // ignore: literal_only_boolean_expressions
  while (true) {
    sleep(const Duration(seconds: 1));
  }
}
