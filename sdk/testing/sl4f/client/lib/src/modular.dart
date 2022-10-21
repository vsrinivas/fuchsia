// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:convert';

import 'package:logging/logging.dart';

import 'exceptions.dart';
import 'sl4f_client.dart';

final _log = Logger('modular');

/// The function type for making a generic request to Modular.
typedef ModularRequestFn = Future<dynamic> Function(String request,
    [dynamic params]);

/// Controls a Modular session and its components.
///
/// See https://fuchsia.dev/fuchsia-src/concepts/modular/overview for more
/// information on Modular.
class Modular {
  /// Function that makes a request to SL4F.
  final ModularRequestFn _request;

  bool _controlsBasemgr = false;

  /// Whether this instance controls the currently running basemgr so it will
  /// shut it down when [shutdown] is called.
  bool get controlsBasemgr => _controlsBasemgr;

  Modular(Sl4f sl4f) : _request = sl4f.request;

  /// Restarts a Modular session.
  ///
  /// This is equivalent to `ffx session restart`.
  Future<String> restartSession() async =>
      await _request('modular_facade.RestartSession');

  /// Kill Basemgr.
  ///
  /// This is equivalent to stopping the session component.
  Future<String> killBasemgr() async =>
      await _request('modular_facade.KillBasemgr');

  /// Launches basemgr.
  ///
  /// This will start a modular session. Note that there can only be one global
  /// basemgr instance running at a time. If there's a chance that basemgr is
  /// already running use [boot] instead which refuse to launch a second
  /// basemgr.
  ///
  /// Takes a custom [config] as JSON serialized string, or launches basemgr
  /// with system default config if not provided.
  ///
  /// If [sessionUrl] provided, the given session component will be launched.
  Future<String> startBasemgr([String config, String sessionUrl]) async {
    final args = {};
    if (config != null && config.isNotEmpty) {
      args['config'] = json.decode(config);
    }
    if (sessionUrl != null) {
      args['session_url'] = sessionUrl;
    }
    return await _request('modular_facade.StartBasemgr', args);
  }

  /// Whether basemgr is currently running on the DUT.
  ///
  /// This works whether it was started by this class or not.
  Future<bool> get isRunning async =>
      await _request('modular_facade.IsBasemgrRunning');

  /// Starts basemgr only if it isn't running yet.
  ///
  /// Takes a custom [config] as JSON serialized string, or launches basemgr
  /// with system default config if not provided.
  ///
  /// If [assumeControl] is true (the default) and basemgr wasn't running, then
  /// this object will stop basemgr when [shutdown] is called with no arguments.
  ///
  /// If [sessionUrl] provided, the given session component will be launched.
  Future<void> boot(
      {String config, bool assumeControl = true, String sessionUrl}) async {
    if (await isRunning) {
      _log.info('Not taking control of basemgr, it was already running.');
      return;
    }

    _log.info('Booting ${sessionUrl} '
        'with ${(config != null) ? 'custom' : 'default'} '
        'configuration.');

    if (sessionUrl == null) {
      _log.warning(
          'The legacy basemgr.cmx component is deprecated. Please update this '
          'client to call boot() with a sessionUrl (https://fxbug.dev/82391)');
    }

    await startBasemgr(config, sessionUrl);

    // basemgr and all the agents can take some time to fully boot.
    var retry = 0;
    while (retry++ <= 60 && !await isRunning) {
      await Future.delayed(const Duration(seconds: 2));
    }

    if (!await isRunning) {
      throw Sl4fException('Timeout for waiting basemgr to boot.');
    }
    if (assumeControl) {
      _controlsBasemgr = true;
    }
  }

  /// Stops basemgr if it is controlled by this instance, or if
  /// [forceShutdownBasemgr] is true.
  Future<void> shutdown({bool forceShutdownBasemgr = false}) async {
    if (!forceShutdownBasemgr && !_controlsBasemgr) {
      _log.info(
          'Modular SL4F client does not control basemgr, not shutting it down');
      return;
    }
    if (!await isRunning) {
      _log.info('Basemgr does not seem to be running before shutdown.');
      // Running `basemgr_launcher shutdown` when basemgr is not running
      // produces a very confusing stacktrace in the logs, so we avoid it here.
      return;
    }
    _log.info('Shutting down basemgr.');
    await killBasemgr();
    // basemgr and all the agents can take some time to fully die.

    final timer = Stopwatch();
    timer.start();
    final timeout = Duration(seconds: 30).inSeconds;
    while (timer.elapsed.inSeconds < timeout && await isRunning) {
      await Future.delayed(const Duration(seconds: 2));
    }
    _log.info(
        'Waited ${timer.elapsed.inSeconds} seconds for Basemgr to shutdown.');

    if (await isRunning) {
      throw Sl4fException('Timeout for waiting basemgr to shut down.');
    }
    _controlsBasemgr = false;
  }

  /// Updates [extraArgs] to [componentPattern] in the modular session [config]
  /// in json. We assume that the component_args are unique, and overwrites
  /// args in [extraArgs] that are already exists in [config].
  ///
  /// Note: This method will replace existing args, if your component accepts
  /// repeated args ex:
  /// {
  ///   "args": [
  ///     "--flag_a=1",
  ///     "--flag_a=2"
  ///   ]
  /// }
  /// then calling this method with [extraArgs] = {"flag_a": 3, "flag_a": 4}
  /// will resulted in:
  /// {
  ///   "args": [
  ///     "--flag_a=3",
  ///     "--flag_a=4"
  ///   ]
  /// }
  /// Note: only args from the first matched [componentPattern] is updated.
  String updateComponentArgs(
      String config, RegExp componentPattern, Map<String, dynamic> extraArgs) {
    dynamic configJson = jsonDecode(config);
    final componentArgsBody = configJson['sessionmgr']['component_args'];
    final componentArgs = componentArgsBody?.firstWhere(
        (e) =>
            e['uri'] is String &&
            componentPattern.hasMatch(Uri.parse(e['uri']).fragment),
        orElse: () => _log.warning('No component matched $componentPattern'));

    if (componentArgs != null) {
      // Update all the flags in [args].
      componentArgs['args']?.removeWhere((currentArg) => extraArgs.keys
          .contains(currentArg.split('=')[0].replaceFirst('--', '')));
      componentArgs['args']?.addAll(extraArgs
          .map((arg, val) =>
              MapEntry(val != null ? '--$arg=$val' : '--$arg', ''))
          .keys);
    }
    return jsonEncode(configJson);
  }
}
