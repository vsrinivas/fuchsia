// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:args/args.dart';
import 'package:cloud_indexer/auth_manager.dart';
import 'package:cloud_indexer/module_uploader.dart';
import 'package:cloud_indexer/request_handler.dart';
import 'package:cloud_indexer_common/config.dart';
import 'package:gcloud/service_scope.dart' as ss;
import 'package:logging/logging.dart';
import 'package:shelf/shelf.dart' as shelf;
import 'package:shelf/shelf_io.dart' as io;

Logger _logger = new Logger('cloud_indexer.shelf');

class FakeAuthManager implements AuthManager {
  Future<bool> checkAuthenticated(String accessToken) async => true;
}

Future runDefaultService(int port, bool authIsDisabled) {
  if (authIsDisabled) {
    _logger.info('The server will be started without authentication.');
  }

  return ss.fork(() async {
    final Config config = await Config.create();
    registerConfigService(config);
    final AuthManager authManager = authIsDisabled
        ? new FakeAuthManager()
        : await AuthManager.fromServiceScope();
    registerAuthManagerService(authManager);
    final ModuleUploader moduleUploader = new ModuleUploader.fromServiceScope();
    registerModuleUploaderService(moduleUploader);

    _logger.info('Starting up server on port: $port');
    io.serve(
        const shelf.Pipeline()
            .addMiddleware(shelf.logRequests())
            .addHandler(requestHandler),
        'localhost',
        port);

    // We use this to prevent fall-through since io.serve completes immediately.
    return new Completer().future;
  });
}

main(List<String> args) {
  Logger.root.level = Level.ALL;
  Logger.root.onRecord.listen((LogRecord rec) {
    print('${rec.level.name}: ${rec.time}: ${rec.message}');
  });

  final ArgParser parser = new ArgParser()
    ..addSeparator('Usage: dart shelf.dart [--disable-auth] port')
    ..addSeparator('Options:')
    ..addFlag('disable-auth',
        defaultsTo: false,
        negatable: false,
        help: 'Disables OAuth2 authentication.')
    ..addFlag('help',
        defaultsTo: false,
        negatable: false,
        help: 'Displays this help message.');

  final ArgResults results = parser.parse(args);
  if (results['help']) {
    print(parser.usage);
    return;
  }

  int port = 8080;
  if (results.rest.length > 0) port = int.parse(results.rest[0]);
  runDefaultService(port, results['disable-auth']);
}
