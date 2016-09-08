// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:appengine/appengine.dart';
import 'package:cloud_indexer/auth_manager.dart';
import 'package:cloud_indexer/module_uploader.dart';
import 'package:cloud_indexer/request_handler.dart';
import 'package:gcloud/service_scope.dart' as ss;
import 'package:shelf/shelf_io.dart' as io;

main(List<String> args) {
  int port = 8080;
  if (args.length > 0) port = int.parse(args[0]);
  useLoggingPackageAdaptor();
  withAppEngineServices(() {
    return ss.fork(() async {
      final AuthManager authManager = await AuthManager.fromClient(
          authClientService, Platform.environment['INDEXER_BUCKET_NAME']);
      registerAuthManagerService(authManager);
      final ModuleUploader moduleUploader = new ModuleUploader.fromClient(
          authClientService,
          Platform.environment['TOPIC_NAME'],
          Platform.environment['MODULE_BUCKET_NAME']);
      registerModuleUploaderService(moduleUploader);
      return runAppEngine((HttpRequest request) {
        return io.handleRequest(request, requestHandler);
      }, port: port);
    });
  });
}
