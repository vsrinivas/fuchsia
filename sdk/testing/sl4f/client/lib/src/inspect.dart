// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:logging/logging.dart';

final _log = Logger('inspect');

Future<dynamic> _defaultSleep(Duration delay) => Future.delayed(delay);

class Inspect {
  final Future<Process> Function(String) createSshProcess;
  final Future<dynamic> Function(Duration) sleep;

  /// Construct an [Inspect] object.
  ///
  /// [createSshProcess] should typically be sl4f's sshProcess function.
  Inspect(this.createSshProcess, {this.sleep = _defaultSleep});

  /// Obtains the root inspect object for a component whose path includes
  /// [componentName].
  Future<dynamic> inspectComponentRoot(String componentName) async {
    var hubEntries = StringBuffer();
    for (final entry in (await retrieveHubEntries(filter: componentName))) {
      hubEntries.write('$entry ');
    }
    if (hubEntries.isEmpty) {
      _log.severe('No components with name $componentName!');
      return null;
    }

    final jsonResult = await inspectRecursively(hubEntries.toString());
    if (jsonResult == null) {
      _log.severe('Failed to inspect $componentName!');
      return null;
    }

    return jsonResult.single['contents']['root'];
  }

  /// Retrieves the inpect node(s) of [hubEntries], recursively, as a json object.
  Future<dynamic> inspectRecursively(String hubEntries) async {
    final stringInspectResult = await _stdOutForSshCommand(
        'iquery --format=json --recursive $hubEntries');

    if (stringInspectResult == null) {
      _log.severe('Failed to recursively inspect $hubEntries!');
      return null;
    }

    return json.decode(stringInspectResult);
  }

  /// Retrieves a list of hub entries.
  ///
  /// If [filter] is set, only those entries containing [filter] are returned.
  Future<List<String>> retrieveHubEntries({String filter}) async {
    final stringFindResult = await _stdOutForSshCommand('iquery --find /hub');

    if (stringFindResult == null) {
      _log.severe('Failed to find hub!');
      return null;
    }

    final hubEntries = stringFindResult.split('\n').where((line) {
      if (filter == null) {
        return true;
      }
      return line.contains(filter);
    }).toList();

    if (hubEntries.isEmpty) {
      if (filter != null) {
        _log.severe('No hub entries with $filter in their path!');
      } else {
        _log.severe('No hub entries found!');
      }
      return null;
    }
    return hubEntries;
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
      final process = await createSshProcess(command);
      final exitCode = await process.exitCode;
      final stdout = await process.stdout.transform(utf8.decoder).join();
      if (exitCode != 0) {
        final stderr = await process.stderr.transform(utf8.decoder).join();
        _log.severe('Command "$command" failed with exit code $exitCode');
        if (stdout.isNotEmpty) {
          _log.severe('stdout: $stdout');
        }
        if (stderr.isNotEmpty) {
          _log.severe('stderr: $stderr');
        }
        return null;
      }
      return stdout;
    }

    final result = await attempt();
    if (result != null) {
      return result;
    }
    var delay = initialDelay;
    for (var i = 0; i < retries; i++) {
      _log.info('Command failed on attempt ${i + 1} of ${retries + 1}.'
          '  Waiting ${delay.inMilliseconds}ms before trying again...');
      await sleep(delay);
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
