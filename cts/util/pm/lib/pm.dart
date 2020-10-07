// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:core';
import 'dart:io';

import 'package:async/async.dart';
import 'package:logging/logging.dart';
import 'package:net/curl.dart';
import 'package:net/ports.dart';
import 'package:path/path.dart' as path;
import 'package:quiver/core.dart' show Optional;
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:retry/retry.dart';
import 'package:test/test.dart';

class PackageManagerRepo {
  final sl4f.Sl4f _sl4fDriver;
  final String _pmPath;
  final String _repoPath;
  final Logger _log;
  Optional<Process> _serveProcess;
  Optional<int> _servePort;
  Optional<StreamSplitter<List<int>>> _serveStdout;
  Optional<StreamSplitter<List<int>>> _serveStderr;

  Optional<Process> getServeProcess() => _serveProcess;
  Optional<int> getServePort() => _servePort;
  String getRepoPath() => _repoPath;

  /// Uses [StreamSplitter] to split the serve process's stdout so there can
  /// be multiple listeners for a single stream source.
  ///
  /// This is useful because we are listening to stdout to determine if serve
  /// has successfully started up, but tests want to be able to listen for
  /// their own purposes as well.
  Stream<List<int>> getServeStdoutSplitStream() {
    if (_serveProcess.isNotPresent) {
      throw Exception(
          'Trying to get stdout from a process that does not exist.');
    }
    if (_serveStdout.isNotPresent) {
      _serveStdout =
          Optional.of(StreamSplitter<List<int>>(_serveProcess.value.stdout));
    }
    return _serveStdout.value.split();
  }

  /// Uses [StreamSplitter] to split the serve process's stderr so there can
  /// be multiple listeners for a single stream source.
  Stream<List<int>> getServeStderrSplitStream() {
    if (_serveProcess.isNotPresent) {
      throw Exception(
          'Trying to get stderr from a process that does not exist.');
    }
    if (_serveStderr.isNotPresent) {
      _serveStderr =
          Optional.of(StreamSplitter<List<int>>(_serveProcess.value.stderr));
    }
    return _serveStderr.value.split();
  }

  PackageManagerRepo._create(
      this._sl4fDriver, this._pmPath, this._repoPath, this._log) {
    _serveProcess = Optional.absent();
    _servePort = Optional.absent();
    _serveStdout = Optional.absent();
    _serveStderr = Optional.absent();
  }

  static Future<PackageManagerRepo> initRepo(
      sl4f.Sl4f sl4fDriver, String pmPath, Logger log) async {
    var repoPath = (await Directory.systemTemp.createTemp('repo')).path;
    return PackageManagerRepo._create(sl4fDriver, pmPath, repoPath, log);
  }

  /// Create new repo using `pm newrepo`.
  ///
  /// Uses this command:
  /// `pm newrepo -repo=<repo path>`
  Future<ProcessResult> pmNewrepoRepo() async {
    _log.info('Initializing repo: $_repoPath');
    return Process.run(_pmPath, ['newrepo', '-repo=$_repoPath']);
  }

  /// Publish an archive to a repo using `pm publish`.
  ///
  /// Uses this command:
  /// `pm publish -a -f=<archive path> -repo=<repo path>`
  Future<ProcessResult> pmPublishAFRepo(String archivePath) async {
    _log.info('Publishing $archivePath to repo.');
    return Process.run(
        _pmPath, ['publish', '-a', '-f=$archivePath', '-repo=$_repoPath']);
  }

  /// Create archive for a given manifest using `pm archive`.
  ///
  /// Uses this command:
  /// `pm -m=<manifest path> archive`
  Future<ProcessResult> pmMArchive(String packageManifestPath) async {
    _log.info('Creating archive from a given package manifest.');
    // Running the process from runtime_deps directory is needed since meta.far needs to be
    // a relative path.
    final workingDirectory =
        Platform.script.resolve('runtime_deps').toFilePath();
    return Process.run(_pmPath, ['-m=$packageManifestPath', 'archive'],
        workingDirectory: workingDirectory);
  }

  /// Wait for the serve process's stdout to report its status.
  ///
  /// Listens to the given process's stdout for either
  /// `[pm serve] serving /repo/path at http://[::]:55555`
  /// or
  /// `bind: address already in use`
  ///
  /// `timeout` parmeter dictates how long to wait for the serve to start.
  /// This defaults to 10 seconds - arbitrarily chosen assuming test servers
  /// are very slow.
  Future waitForServeStartup(Process process,
      {Duration timeout = const Duration(seconds: 10)}) {
    final completer = Completer();

    RegExp servingRegex = RegExp(r'\[pm serve\] serving.+ at http.+:(\d+)');

    getServeStdoutSplitStream().transform(utf8.decoder).listen((data) {
      print('[pm test][stdout] $data');
      var servingMatch = servingRegex.firstMatch(data);
      if (servingMatch != null) {
        if (_servePort.isPresent) {
          // Check the port if we can.
          expect(servingMatch.group(1), _servePort.value.toString());
        }
        completer.complete(true);
      }
    });

    getServeStderrSplitStream().transform(utf8.decoder).listen((data) {
      print('[pm test][stderr] $data');
      if (data.contains('address already in use')) {
        completer.complete(false);
      }
    });

    Timer(timeout, () {
      if (!completer.isCompleted) {
        completer.completeError(
            'Timed out waiting for serve to print its own status.');
      }
    });

    return completer.future;
  }

  /// Attempts to start the `serve` process.
  ///
  /// `port` is optional, but if it is given then `curl` will be used as an
  /// additional check for whether `serve` has successfully started.
  ///
  /// Returns `true` if serve startup was successful.
  Future<bool> tryServe(List<String> args, {int port}) async {
    resetServe();
    _serveProcess = Optional.of(await Process.start(_pmPath, args));

    // Watch `serve`'s prints if we can.
    // However, the `-q` flag makes its output silent.
    // If `serve` is silent, skip and only check based on `curl`.
    if (!args.contains('-q')) {
      if (!await waitForServeStartup(_serveProcess.value)) {
        return false;
      }
    }

    // Check if `serve` is up using `curl`.
    // However, if no port number is given then we do not know the URL and
    // cannot do this check.
    //
    // NOTE: It is possible for `-q` to exist as an argument and for there
    // to be no port number given. In such a case, this method will simply
    // start the process and return `true`. In practice, none of the tests
    // do this. If they do in the future, there is not much we can do anyway.
    if (port != null) {
      _log.info('Wait until serve responds to curl.');
      final curlStatus = await retryWaitForCurlHTTPCode(
          ['http://localhost:$port/targets.json'], 200,
          logger: _log);
      _log.info('curl return code: $curlStatus');
      if (curlStatus != 0) {
        return false;
      }
    }
    return true;
  }

  /// Start a package server using `pm serve` with serve-selected port.
  ///
  /// Passes in `-l :0` to tell `serve` to choose its own port.
  /// `-f <port file path>` saves the chosen port number to a file.
  ///
  /// Does not return until the port file is created, or times out.
  ///
  /// Uses this command:
  /// `pm serve -repo=<repo path> -l :0 -f <port file path> [extraArgs]`
  Future<void> pmServeRepoLFExtra(List<String> extraServeArgs) async {
    final portFilePath = path.join(_repoPath, 'port_file.txt');
    List<String> arguments = [
      'serve',
      '-repo=$_repoPath',
      '-l',
      ':0',
      '-f',
      portFilePath
    ];
    _log.info('Serve is starting.');
    final retryOptions = RetryOptions(maxAttempts: 5);
    // final args = arguments + extraServeArgs
    await retryOptions.retry(() async {
      if (!await tryServe(arguments + extraServeArgs)) {
        throw Exception('Attempt to bringup `pm serve` has failed.');
      }
    });

    _log.info('Waiting until the port file is created at: $portFilePath');
    final portFile = File(portFilePath);
    String portString = await retryOptions.retry(portFile.readAsStringSync);
    expect(portString, isNotNull);
    _servePort = Optional.of(int.parse(portString));
    expect(_servePort.isPresent, isTrue);
    _log.info('Serve started on port: ${_servePort.value}');
  }

  /// Start a package server using `pm serve` with our own port selection.
  ///
  /// Does not return until the serve begins listening, or times out.
  ///
  /// Uses this command:
  /// `pm serve -repo=<repo path> -l :<port number> [extraArgs]`
  Future<void> pmServeRepoLExtra(List<String> extraServeArgs) async {
    await getUnusedPort<Process>((unusedPort) async {
      _servePort = Optional.of(unusedPort);
      List<String> arguments = [
        'serve',
        '-repo=$_repoPath',
        '-l',
        ':$unusedPort'
      ];
      _log.info('Serve is starting on port: $unusedPort');
      if (await tryServe(arguments + extraServeArgs, port: unusedPort)) {
        return _serveProcess.value;
      }
      return null;
    });

    expect(_serveProcess.isPresent, isTrue);
    expect(_servePort.isPresent, isTrue);
  }

  /// Add repo source using `amberctl add_src`.
  ///
  /// Uses this command:
  /// `amberctl add_src -n <name> -f <config>`
  Future<ProcessResult> amberctlAddSrcNF(
      String msg, String name, String config, int retCode) async {
    return _sl4fRun(
        msg, 'amberctl add_src', ['-n $name', '-f $config'], retCode,
        randomize: true);
  }

  /// Add repo source using `amberctl add_src`.
  ///
  /// Uses this command:
  /// `amberctl add_src -f <config>`
  Future<ProcessResult> amberctlAddSrcF(
      String msg, String config, int retCode) async {
    return _sl4fRun(msg, 'amberctl add_src', ['-f $config'], retCode);
  }

  /// Remove a repo source using `amberctl rm_src`.
  ///
  /// Uses this command:
  /// `amberctl rm_src -n <repo name>`
  Future<ProcessResult> amberctlRmsrcN(
      String msg, String repoName, int retCode) async {
    return _sl4fRun(msg, 'amberctl rm_src', ['-n $repoName'], retCode);
  }

  /// Enable a named repo source using `amberctl enable_src`.
  ///
  /// Uses this command:
  /// `amberctl enable_src -n <source>`
  Future<ProcessResult> amberctlEnablesrcN(
      String msg, String source, int retCode) async {
    return _sl4fRun(msg, 'amberctl enable_src', ['-n $source'], retCode);
  }

  /// Add repo source using `amberctl add_repo_cfg`.
  ///
  /// This does not set a rewrite rule to use the new config.
  ///
  /// Uses this command:
  /// `amberctl add_repo_cfg -n <repo path> -f <config>`
  Future<ProcessResult> amberctlAddrepocfgNF(
      String msg, String config, int retCode) async {
    return _sl4fRun(
        msg, 'amberctl add_repo_cfg', ['-n $_repoPath', '-f $config'], retCode,
        randomize: true);
  }

  /// Get the named component from the repo using `pkgctl resolve`.
  ///
  /// Uses this command:
  /// `pkgctl resolve <component URL>`
  Future<ProcessResult> pkgctlResolve(
      String msg, String url, int retCode) async {
    return _sl4fRun(msg, 'pkgctl resolve', [url], retCode);
  }

  /// List repo sources using `pkgctl repo`.
  ///
  /// Uses this command:
  /// `pkgctl repo`
  Future<ProcessResult> pkgctlRepo(String msg, int retCode) async {
    return _sl4fRun(msg, 'pkgctl repo', [], retCode);
  }

  /// List redirect rules using `pkgctl rule list`.
  ///
  /// Uses this command:
  /// `pkgctl rule list`
  Future<ProcessResult> pkgctlRuleList(String msg, int retCode) async {
    return _sl4fRun(msg, 'pkgctl rule list', [], retCode);
  }

  Future<ProcessResult> _sl4fRun(
      String msg, String cmd, List<String> params, int retCode,
      {bool randomize = false}) async {
    _log.info(msg);
    if (randomize) {
      params.shuffle();
    }
    var cmdBuilder = StringBuffer()..write(cmd)..write(' ');
    for (var param in params) {
      cmdBuilder..write(param)..write(' ');
    }
    final response = await _sl4fDriver.ssh.run(cmdBuilder.toString());
    expect(response.exitCode, retCode);
    return response;
  }

  Future<bool> setupRepo(String farPath, String manifestPath) async {
    var responses =
        await Future.wait([pmNewrepoRepo(), pmMArchive(manifestPath)]);
    expect(responses.length, 2);
    // Response for creating a new repo.
    expect(responses[0].exitCode, 0);
    // Response for creating a `.far` archive file.
    expect(responses[1].exitCode, 0);

    final archivePath =
        Platform.script.resolve('runtime_deps/$farPath').toFilePath();
    _log.info(
        'Publishing package from archive: $archivePath to repo: $_repoPath');
    final publishPackageResponse = await pmPublishAFRepo(archivePath);
    expect(publishPackageResponse.exitCode, 0);

    return true;
  }

  Future<bool> setupServe(
      String farPath, String manifestPath, List<String> extraServeArgs) async {
    await setupRepo(farPath, manifestPath);
    await pmServeRepoLExtra(extraServeArgs);
    return true;
  }

  void resetServe() {
    if (_serveStdout.isPresent) {
      _serveStdout.value.close();
      _serveStdout = Optional.absent();
    }
    if (_serveStderr.isPresent) {
      _serveStderr.value.close();
      _serveStderr = Optional.absent();
    }
    if (_serveProcess.isPresent) {
      _serveProcess.value.kill();
      _serveProcess = Optional.absent();
    }
  }

  bool kill() {
    bool success = true; // Allows scaling to more things later.
    if (_serveProcess.isPresent) {
      success &= _serveProcess.value.kill();
    }
    return success;
  }

  void cleanup() {
    Directory(_repoPath).deleteSync(recursive: true);
    resetServe();
  }
}
