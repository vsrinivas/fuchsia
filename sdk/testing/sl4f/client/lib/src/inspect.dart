// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

import 'package:logging/logging.dart';

import 'ssh.dart';

final _log = Logger('inspect');

class Inspect {
  final Ssh ssh;

  /// Construct an [Inspect] object.
  Inspect(this.ssh);

  /// Obtains the root inspect object for a component whose path includes
  /// [componentName].
  ///
  /// This is equivalent to calling retrieveHubEntries and inspectRecursively in
  /// series.
  /// Returns null when there are no entries matching.
  Future<dynamic> inspectComponentRoot(Pattern componentName) async {
    final entries = await retrieveHubEntries(filter: componentName);
    if (entries.isEmpty) {
      return null;
    }

    final jsonResult = await inspectRecursively(entries);
    if (jsonResult == null) {
      return null;
    }

    return jsonResult.single['contents']['root'];
  }

  /// Retrieves the inpect node(s) of [hubEntries], recursively, as a json
  /// object.
  ///
  /// Returns null if there's no inspect information matching for those entries.
  /// Otherwise a parsed JSON as formated by
  /// //garnet/public/lib/inspect_deprecated/query/json_formatter.cc is
  /// returned.
  Future<dynamic> inspectRecursively(List<String> entries) async {
    final hubEntries = entries.join(' ');
    final stringInspectResult = await _stdOutForSshCommand(
        'iquery --format=json --recursive $hubEntries');

    if (stringInspectResult == null) {
      return null;
    }

    return json.decode(stringInspectResult);
  }

  /// Retrieves a list of hub entries.
  ///
  /// If [filter] is set, only those entries containing [filter] are returned.
  /// If there are no matches, an empty list is returned.
  Future<List<String>> retrieveHubEntries({Pattern filter}) async {
    final stringFindResult = await _stdOutForSshCommand('iquery --find /hub');
    if (stringFindResult == null) {
      return [];
    }

    return stringFindResult
        .split('\n')
        .where((line) => filter == null || line.contains(filter))
        .toList();
  }

  /// Runs [command] in an ssh process to completion.
  ///
  /// Returns stdout of that process or null if the process exited with non-zero
  /// exit code.
  /// Upon failure, the command will be retried [retries] times. With an
  /// exponential backoff delay in between.
  Future<String> _stdOutForSshCommand(String command,
      {int retries = 3,
      Duration initialDelay = const Duration(seconds: 1)}) async {
    Future<String> attempt() async {
      final process = await ssh.run(command);
      return process.exitCode != 0 ? null : process.stdout;
    }

    final result = await attempt();
    if (result != null) {
      return result;
    }
    var delay = initialDelay;
    for (var i = 0; i < retries; i++) {
      _log.info('Command failed on attempt ${i + 1} of ${retries + 1}.'
          '  Waiting ${delay.inMilliseconds}ms before trying again...');
      await Future.delayed(delay);
      final result = await attempt();
      if (result != null) {
        return result;
      }
      delay *= 2;
    }
    _log.info('Command failed on attempt ${retries + 1} of ${retries + 1}.'
        '  No more retries remain.');
    return null;
  }
}
