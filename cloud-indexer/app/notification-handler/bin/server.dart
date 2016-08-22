// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:appengine/appengine.dart';
import 'package:notification_handler/src/request_handler.dart';

main(List<String> args) {
  int port = 8080;
  if (args.length > 0) port = int.parse(args[0]);
  runAppEngine(requestHandler, port: port);
}
