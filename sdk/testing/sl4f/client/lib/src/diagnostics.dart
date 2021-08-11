// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'package:logging/logging.dart';

import 'dump.dart';
import 'sl4f_client.dart';

final _log = Logger('diagnostics');

/// Generate diagnostics info using [Sl4f].
class Diagnostics {
  final Sl4f _sl4f;
  final Dump _dump;

  /// Constructs a [Diagnostics] object.
  Diagnostics(this._sl4f, [Dump dump]) : _dump = dump ?? Dump();

  Future<void> dumpDiagnostics(String dumpName) async {
    if (_dump.hasDumpDirectory) {
      await dumpWlan(dumpName);
      await dumpNetif(dumpName);
    }
  }

  Future<void> dumpNetif(String dumpName) async {
    final fileName = '$dumpName-diagnostic-net-if';
    _log.info('dump net-if as $fileName');
    try {
      final result = await _sl4f.request('netstack_facade.ListInterfaces', {});
      if (result == null) {
        return null;
      }
      return _dump.writeAsBytes(
          fileName, 'json', json.encode(result).codeUnits);
    } on Exception catch (e) {
      return _dump.writeAsString('error-$fileName', 'txt', 'error catch: $e');
    }
  }

  Future<void> dumpWlan(String dumpName) async {
    final fileName = '$dumpName-diagnostic-wlan';
    _log.info('dump wlan status as $fileName');
    try {
      final result = await _sl4f.request('wlan.status', {});
      if (result == null) {
        return null;
      }
      return _dump.writeAsBytes(
          fileName, 'json', json.encode(result).codeUnits);
    } on Exception catch (e) {
      return _dump.writeAsString('error-$fileName', 'txt', 'error catch: $e');
    }
  }
}
