// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:appengine/appengine.dart';
import 'package:cloud_indexer/auth_manager.dart';
import 'package:cloud_indexer/module_uploader.dart';
import 'package:cloud_indexer/request_handler.dart';
import 'package:gcloud/pubsub.dart';
import 'package:gcloud/service_scope.dart' as ss;
import 'package:shelf/shelf_io.dart' as io;

const String _projectName = 'modular-cloud-indexer';

main(List<String> args) {
  int port = 8080;
  if (args.length > 0) port = int.parse(args[0]);
  useLoggingPackageAdaptor();
  withAppEngineServices(() {
    return ss.fork(() async {
      final PubSub pubSub = new PubSub(authClientService, _projectName);
      registerPubSubService(pubSub);
      final AuthManager authManager =
          new AuthManager.fromClient(authClientService);
      registerAuthManagerService(authManager);
      final ModuleUploader moduleUploader =
          await ModuleUploader.createModuleUploader(pubSub: pubSub);
      registerModuleUploaderService(moduleUploader);
      return runAppEngine((HttpRequest request) {
        return io.handleRequest(request, requestHandler);
      }, port: port);
    });
  });
}
