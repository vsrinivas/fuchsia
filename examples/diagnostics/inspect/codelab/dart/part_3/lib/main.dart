// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fidl_fuchsia_examples_inspect/fidl_async.dart' as fidl_codelab;
import 'package:fuchsia_inspect/inspect.dart' as inspect;
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
import 'package:inspect_dart_codelab_part_3_lib/reverser.dart';

void main(List<String> args) {
  final context = ComponentContext.create();

  setupLogger(name: 'inspect_dart_codelab', globalTags: ['part_3']);

  log.info('Starting up...');

  final inspector = inspect.Inspect()..serve(context.outgoing);

  inspector.health.setStartingUp();

  inspector.root.stringProperty('version').setValue('part3');

  final fizzBuzz = fidl_codelab.FizzBuzzProxy();
  context.svc.connectToService(fizzBuzz);
  fizzBuzz.execute(30).timeout(const Duration(seconds: 2), onTimeout: () {
    throw Exception('timeout');
  }).then((result) {
    log.info('Got FizzBuzz: $result');
    inspector.health.setOk();
  }).catchError((e) {
    inspector.health.setUnhealthy('FizzBuzz connection closed');
  });

  context.outgoing
    ..addPublicService<fidl_codelab.Reverser>(
      ReverserImpl.getDefaultBinder(inspector.root.child('reverser_service')),
      fidl_codelab.Reverser.$serviceName,
    )
    ..serveFromStartupInfo();
}
