// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

import 'package:logging/logging.dart';

import 'component.dart';
import 'sl4f_client.dart';

final _log = Logger('modular');

/// The function type for making a generic request to Modular.
typedef ModularRequestFn = Future<dynamic> Function(String request,
    [dynamic params]);

/// Allows controlling a Modular session and its components.
class Modular {
  /// The function used to make a custom request for Modular.
  final ModularRequestFn _request;

  /// The handle to a component search query issuer.
  final Component _component;

  bool _controlsBasemgr = false;

  /// Whether this instance controls the currently running basemgr so it will
  /// shut it down when [shutdown] is called.
  bool get controlsBasemgr => _controlsBasemgr;

  Modular(Sl4f sl4f, {Component component})
      : _request = sl4f.request,
        _component = component ?? Component(sl4f);

  /// Restarts a Modular session.
  ///
  /// This is equivalent to sessionctl restart_session.
  Future<String> restartSession() async =>
      await _request('basemgr_facade.RestartSession');

  /// Kill Basemgr.
  ///
  /// This is equivalent to basemgr_launcher shutdown.
  Future<String> killBasemgr() async =>
      await _request('basemgr_facade.KillBasemgr');

  /// Launches Basemgr.
  ///
  /// Takes a custom [config] as JSON serialized string, or launches basemgr
  /// with system default config if not provided.
  Future<String> startBasemgr([String config]) async {
    if (config != null && config.isNotEmpty) {
      return await _request(
          'basemgr_facade.StartBasemgr', {'config': json.decode(config)});
    } else {
      return await _request('basemgr_facade.StartBasemgr', {});
    }
  }

  /// Launches Mod.
  ///
  /// Take custom parameters or launch mod with default value.
  Future<String> launchMod(String modUrl,
      {String modName, String storyName}) async {
    return await _request('basemgr_facade.LaunchMod',
        {'mod_url': modUrl, 'mod_name': modName, 'story_name': storyName});
  }

  /// Whether basemgr is running on the DUT.
  Future<bool> get isRunning => _component.search('basemgr.cmx');

  /// Starts basemgr if it isn't running yet.
  ///
  /// Takes a custom [config] as JSON serialized string, or launches basemgr
  /// with system default config if not provided.
  ///
  /// If [assumeControl] is true (the default) and basemgr wasn't running, then
  /// this object will stop basemgr when [shutdown] is called with no arguments.
  Future<void> boot({String config, bool assumeControl = true}) async {
    if (await isRunning) {
      _log.info('Not taking control of basemgr, it was already running.');
      return;
    }

    _log.info('Booting basemgr with ${(config != null) ? 'custom' : 'default'} '
        'configuration.');
    await startBasemgr(config);
    await Future.delayed(Duration(seconds: 10));
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
    await Future.delayed(Duration(seconds: 5));
    _controlsBasemgr = false;
  }
}
