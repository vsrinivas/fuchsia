// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io';
import 'package:fuchsia/fuchsia.dart' as fuchsia;
import 'package:inspect_codelab_shared/codelab_environment.dart';

Future<void> main(List<String> args) async {
  if (args.length < 2 || int.tryParse(args[0]) == null) {
    print('Usage: <program> <example-number> <string> <string> ....');
    fuchsia.exit(1);
  }

  final serverName = 'inspect-dart-codelab-part-${int.parse(args[0])}';
  final reverserUrl =
      'fuchsia-pkg://fuchsia.com/$serverName#meta/$serverName.cmx';

  final env = CodelabEnvironment();
  await env.create();
  await env.startFizzBuzz();
  final reverser = await env.startReverser(reverserUrl);

  // [START reverse_loop]
  for (int i = 1; i < args.length; i++) {
    print('Input: ${args[i]}');
    final response = await reverser.reverse(args[i]);
    print('Output: $response');
  }
  // [END reverse_loop]

  print('Done. Press Ctrl+C to exit');

  // ignore: literal_only_boolean_expressions
  while (true) {
    sleep(const Duration(seconds: 1));
  }
}
