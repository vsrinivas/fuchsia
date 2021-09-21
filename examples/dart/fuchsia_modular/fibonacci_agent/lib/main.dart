// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fuchsia_logger/logger.dart';

import 'package:fuchsia_modular/agent.dart';
import 'package:fuchsia_services/services.dart';
import 'src/fibonacci_service_impl.dart';

void main(List<String> args) {
  setupLogger(name: 'fibonacci-agent');
  final context = ComponentContext.createAndServe();
  Agent()
    ..exposeService(FibonacciServiceImpl())
    ..serve(context.outgoing);
}
