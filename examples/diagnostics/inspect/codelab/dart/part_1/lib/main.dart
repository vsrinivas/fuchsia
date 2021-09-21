// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fidl_fuchsia_examples_inspect/fidl_async.dart' as fidl_codelab;
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
import 'package:inspect_dart_codelab_part_1_lib/reverser.dart';
// CODELAB: use inspect.

void main(List<String> args) async {
  // [START init_logger]
  setupLogger(name: 'inspect_dart_codelab', globalTags: ['part_1']);
  // [END init_logger]

  log.info('Starting up...');

  // CODELAB: Initialize Inspect here.

  // [START serve_service]
  final context = ComponentContext.create();
  context.outgoing
    ..addPublicService<fidl_codelab.Reverser>(
      ReverserImpl.getDefaultBinder(),
      fidl_codelab.Reverser.$serviceName,
    )
    ..serveFromStartupInfo();
  // [END serve_service]

  try {
    // [START connect_fizzbuzz]
    final fizzBuzz = fidl_codelab.FizzBuzzProxy();
    context.svc.connectToService(fizzBuzz);
    final result = await fizzBuzz.execute(30);
    // [END connect_fizzbuzz]
    log.info('Got FizzBuzz: $result');
  } on Exception catch (_) {}
}
