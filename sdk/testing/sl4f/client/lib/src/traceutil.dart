// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be// found in the LICENSE file.

import 'dart:io' show File;

import 'package:meta/meta.dart';

import 'dump.dart';
import 'performance.dart';
import 'sl4f_client.dart';

// TODO(PT-178): This class has been migrated and merged with Performance.
class Traceutil {
  final Performance _performance;
  Traceutil(Sl4f _sl4f, Dump _dump) : _performance = Performance(_sl4f, _dump);
  Future<bool> trace(
          {@required Duration duration,
          @required String traceName,
          String categories,
          int bufferSize,
          bool binary = false,
          bool compress = false}) =>
      _performance.trace(
          duration: duration,
          traceName: traceName,
          categories: categories,
          bufferSize: bufferSize,
          binary: binary,
          compress: compress);

  Future<File> downloadTraceFile(String traceName,
          {bool binary = false, bool compress = false}) async =>
      _performance.downloadTraceFile(traceName,
          binary: binary, compress: compress);
}
