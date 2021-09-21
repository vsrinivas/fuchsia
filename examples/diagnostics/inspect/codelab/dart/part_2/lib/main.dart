// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fidl_fuchsia_examples_inspect/fidl_async.dart' as fidl_codelab;
// [START part_1_import_inspect]
import 'package:fuchsia_inspect/inspect.dart' as inspect;
// [END part_1_import_inspect]
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
import 'package:inspect_dart_codelab_part_2_lib/reverser.dart';

void main(List<String> args) {
  final context = ComponentContext.create();

  setupLogger(name: 'inspect_dart_codelab', globalTags: ['part_2']);

  log.info('Starting up...');

  // [START part_1_init_inspect]
  final inspector = inspect.Inspect()..serve(context.outgoing);
  // [END part_1_init_inspect]

  // [START part_1_write_version]
  inspector.root.stringProperty('version').setValue('part2');
  // [END part_1_write_version]

  // CODELAB: Instrument our connection to FizzBuzz using Inspect. Is there an error?
  // [START instrument_fizzbuzz]
  final fizzBuzz = fidl_codelab.FizzBuzzProxy();
  context.svc.connectToService(fizzBuzz);

  fizzBuzz.execute(30).timeout(const Duration(seconds: 2), onTimeout: () {
    throw Exception('timeout');
  }).then((result) {
    // CODELAB: Add Inspect here to see if there is a response.
    log.info('Got FizzBuzz: $result');
  }).catchError((e) {
    // CODELAB: Instrument our connection to FizzBuzz using Inspect. Is there an error?
  });
  // [END instrument_fizzbuzz]

  // [START part_1_new_child]
  context.outgoing
    ..addPublicService<fidl_codelab.Reverser>(
      ReverserImpl.getDefaultBinder(inspector.root.child('reverser_service')),
      fidl_codelab.Reverser.$serviceName,
    )
    ..serveFromStartupInfo();
  // [END part_1_new_child]
}
