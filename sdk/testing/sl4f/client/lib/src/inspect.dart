// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';

import 'package:pedantic/pedantic.dart';
import 'package:logging/logging.dart';

import 'sl4f_client.dart';
import 'ssh.dart';

final _log = Logger('inspect');

class Inspect {
  Ssh ssh;
  Sl4f sl4f;

  /// Construct an [Inspect] object.
  // TODO(fxb/48733): make this take only Sl4f once all clients have been migrated.
  Inspect(dynamic sl4fOrSsh) {
    if (sl4fOrSsh is Sl4f) {
      sl4f = sl4fOrSsh;
      ssh = sl4f.ssh;
    } else if (sl4fOrSsh is Ssh) {
      sl4f = null;
      ssh = sl4fOrSsh;
    } else {
      throw ArgumentError('Expect `Ssh` or `Sl4f` for `sl4fOrSsh`');
    }
  }

  /// Gets the inspect data filtering using the given selectors.
  ///
  /// A selector consists of the realm path, component name and a path to a node
  /// or property.
  /// It accepts wildcards.
  /// For example:
  ///   a/*/test.cmx:path/to/*/node:prop
  ///   a/*/test.cmx:root
  ///
  /// See: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/sdk/fidl/fuchsia.diagnostics/selector.fidl
  Future<List<Map<String, dynamic>>> snapshot(List<String> selectors) async {
    final hierarchyList =
        await sl4f.request('diagnostics_facade.SnapshotInspect', {
              'selectors': selectors,
            }) ??
            [];
    return hierarchyList.cast<Map<String, dynamic>>();
  }

  /// Gets the inspect data for all components currently running in the system.
  Future<List<Map<String, dynamic>>> snapshotAll() async {
    return await snapshot([]);
  }

  /// Gets the data of the first found hierarchy matching the given selectors
  /// under root. Returns null of no hierarchy was found.
  Future<Map<String, dynamic>> snapshotRoot(String componentSelector) async {
    final hierarchies = await snapshot(['$componentSelector:root']);
    if (hierarchies.isEmpty) {
      return null;
    }
    return hierarchies[0]['payload']['root'];
  }

  /// Obtains the root inspect object for a component whose path includes
  /// [componentName].
  ///
  /// This is equivalent to calling retrieveHubEntries and inspectRecursively in
  /// series.
  /// Returns null when there are no entries matching.
  /// DEPRECATED.
  Future<dynamic> inspectComponentRoot(Pattern componentName) async {
    final entries = await retrieveHubEntries(filter: componentName);
    if (entries.isEmpty) {
      return null;
    }

    final jsonResult = await inspectRecursively(entries);
    if (jsonResult == null) {
      return null;
    }

    // Workaround for bug:36468
    // TODO(crjohns): Remove after fix rolls.
    if (jsonResult.single['contents']['root'].containsKey('root')) {
      return jsonResult.single['contents']['root']['root'];
    }

    return jsonResult.single['contents']['root'];
  }

  /// Retrieves the inpect node(s) of [hubEntries], recursively, as a json
  /// object.
  ///
  /// Returns null if there's no inspect information matching for those entries.
  /// Otherwise a parsed JSON as formated by
  /// //src/lib/inspect_deprecated/query/json_formatter.cc is
  /// returned.
  /// DEPRECATED.
  Future<dynamic> inspectRecursively(List<String> entries) async {
    final hubEntries = entries.join(' ');
    final stringInspectResult = await _stdOutForSshCommand(
        'iquery --format=json --recursive $hubEntries');

    return stringInspectResult == null
        ? null
        : json.decode(stringInspectResult);
  }

  /// Retrieves a list of hub entries.
  ///
  /// If [filter] is set, only those entries containing [filter] are returned.
  /// If there are no matches, an empty list is returned.
  /// DEPRECATED.
  Future<List<String>> retrieveHubEntries({Pattern filter}) async {
    final stringFindResult = await _stdOutForSshCommand('iquery --find /hub');
    return stringFindResult == null
        ? []
        : stringFindResult
            .split('\n')
            .where((line) => filter == null || line.contains(filter))
            .toList();
  }

  /// Runs [command] in an ssh process to completion.
  ///
  /// Returns stdout of that process or null if the process exited with non-zero
  /// exit code.
  Future<String> _stdOutForSshCommand(String command) async {
    final process = await ssh.run(command);
    if (process.stderr != null && process.stderr.trim().isNotEmpty) {
      // Iquery logs to stderr directories it failed to read. Therefore we log
      // them as warning but continue executing.
      _log.warning('$command stderr: ${process.stderr}');
    }
    return process.exitCode == 0 ? process.stdout : null;
  }
}

/// Attempts to detect freezes related to:
/// TODO(b/139742629): Device freezes for a minute after startup
/// TODO(fxb/35898): FTL operations blocked behind wear leveling.
/// TODO(fxb/31379): Need Implementation of "TRIM" command for flash devices
///
/// Freezes can cause running 'iquery' to hang, so we just run iquery every second
/// and watch how long it takes to execute.  Most executions take less than 1 second
/// so 5 seconds is enough to tell that the system was probably wedged.
///
/// In addition, it provides functions for insight into whether a freeze happened,
/// which can be used for retrying failed tests.
class FreezeDetector {
  final Inspect inspect;

  bool _started = false;
  bool _isFrozen = false;
  bool _freezeHappened = false;
  Completer _c;

  Duration threshold = const Duration(seconds: 5);
  static const _workInterval = Duration(seconds: 1);

  Timer _worker;
  final _lastExecution = Stopwatch();

  FreezeDetector(this.inspect);

  void _workerHandler() async {
    _lastExecution.reset();

    // Start the query we use to detect a freeze
    final retrieveHub = inspect.retrieveHubEntries();

    // Schedule some work to happen if it doesn't complete in time.
    unawaited(retrieveHub.timeout(threshold, onTimeout: () {
      _isFrozen = true;
      _log.info('Freeze Start Detected ${DateTime.now()}');
      _freezeHappened = true;
      _c = Completer();
      return ['timeout'];
    }));

    // Wait for the query to finish.
    await retrieveHub;

    if (_isFrozen) {
      _log.info('Freeze End Detected ${DateTime.now()}');
      _isFrozen = false;
      _log.info('Freeze Duration ${_lastExecution.elapsed}');
      _c.complete();
    }

    final nextWork = _workInterval - _lastExecution.elapsed;

    if (_started) {
      _worker = Timer(nextWork, _workerHandler);
    }
  }

  void start() {
    _log.info('Starting FreezeDetector');
    _started = true;
    Timer.run(_workerHandler);
    _lastExecution.start();
  }

  void stop() async {
    if (!_started) {
      return;
    }
    _log.info('Stopping FreezeDetector');
    _started = false;
    _worker?.cancel();
  }

  Future<void> waitUntilUnfrozen() => _c?.future;

  bool isFrozen() => _isFrozen;
  bool freezeHappened() => _freezeHappened;

  void clearFreezeHappened() {
    _freezeHappened = false;
  }
}
