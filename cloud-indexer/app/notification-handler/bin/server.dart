// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:appengine/appengine.dart';
import 'package:gcloud/service_scope.dart' as ss;
import 'package:notification_handler/index_updater.dart';
import 'package:notification_handler/request_handler.dart';
import 'package:shelf/shelf_io.dart' as io;

main(List<String> args) {
  int port = 8080;
  if (args.length > 0) port = int.parse(args[0]);
  useLoggingPackageAdaptor();
  withAppEngineServices(() {
    return ss.fork(() {
      IndexUpdater indexUpdater = new IndexUpdater.fromClient(
          authClientService, Platform.environment['BUCKET_NAME']);
      registerIndexUpdaterService(indexUpdater);
      return runAppEngine((HttpRequest request) {
        return io.handleRequest(request, requestHandler);
      }, port: port);
    });
  });
}
