// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';

import 'src/_mind_reader_impl.dart';

void main(List<String> args) {
  setupLogger(name: 'mind_reader_server');

  final mindReader = MindReaderImpl();

  // we publish the service in the components /out/public directory.
  // Doing this allows the parent of the process to connect to this service.
  ComponentContext.create().outgoing
    ..addPublicService(mindReader.bind, MindReaderImpl.serviceName)
    ..serveFromStartupInfo();
}
