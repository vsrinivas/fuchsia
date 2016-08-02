// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:mojo/application.dart';
import 'package:mojo_apptest/apptest.dart';

import 'handler_service_test.dart';
import 'ledger_graph_test.dart';
import 'ledger_syncer_test.dart';
import 'module_runner_test.dart';
import 'recipe_runner_test.dart';
import 'user_manager_test.dart';

void allTests(Application app, String url) {
  testModuleRunner();
  testRecipeRuns();
  testLedgerGraph();
  testLedgerSyncer(app);
  testUserManager(app, url);
  testHandlerService();
}

void main(List<String> args, Object handleToken) {
  runAppTests(handleToken, [allTests]);
}
