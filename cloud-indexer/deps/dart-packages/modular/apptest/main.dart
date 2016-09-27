// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:mojo/application.dart';
import 'package:mojo_apptest/apptest.dart';

import 'graph_server_test.dart';
import 'remote_async_graph_test.dart';

void allTests(Application app, String url) {
  testGraphServer();
  testRemoteAsyncGraph();

  // TODO(armansito): Add unit tests for the new StateGraph while fixing
  // https://github.com/domokit/modular/issues/800.
  // TODO(armansito): Unit tests for StateGraph don't have a direct dependency
  // on mojo any more so they don't need to be in an apptest.
}

void main(List<String> args, Object handleToken) {
  runAppTests(handleToken, [allTests]);
}
