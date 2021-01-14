// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
  final context = StartupContext.fromStartupInfo();
  context.outgoing.addPublicService<fidl_codelab.Reverser>(
    ReverserImpl.getDefaultBinder(),
    fidl_codelab.Reverser.$serviceName,
  );
  // [END serve_service]

  try {
    // [START connect_fizzbuzz]
    final fizzBuzz = fidl_codelab.FizzBuzzProxy();
    context.incoming.connectToService(fizzBuzz);
    final result = await fizzBuzz.execute(30);
    // [END connect_fizzbuzz]
    log.info('Got FizzBuzz: $result');
  } on Exception catch (_) {}
}
