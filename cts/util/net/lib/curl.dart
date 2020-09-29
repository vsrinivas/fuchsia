// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:logging/logging.dart';

/// Runs the `curl` command until the response has the expected HTTP code.
///
/// Will stop trying after the given `timeout`.
Future<int> retryWaitForCurlHTTPCode(List<String> args, int expectedHTTPCode,
    {Duration timeout = const Duration(seconds: 30), Logger logger}) async {
  final deadline = DateTime.now().add(timeout);
  while (DateTime.now().isBefore(deadline)) {
    var curlResponse = await Process.run(
        'curl', args + ['-o', '/dev/null', '-w', '"%{http_code}"']);
    var responseStr = curlResponse.stdout.toString();
    if (logger != null) {
      logger.info('curl response code: $responseStr');
    }
    if (responseStr.isNotEmpty &&
        responseStr.contains(expectedHTTPCode.toString())) {
      return curlResponse.exitCode;
    }
  }
  return -1;
}
