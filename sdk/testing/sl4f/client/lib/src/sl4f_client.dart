// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is named sl4f_client.dart since the top-level dart file is names
// sl4f.dart, which would cause problems for src files trying to import this
// file.

import 'dart:async';
import 'dart:convert';
import 'dart:io' show Platform, Process, ProcessStartMode, SocketException;

import 'package:http/http.dart' as http;
import 'package:logging/logging.dart';

import 'exceptions.dart';
import 'inspect.dart';

final _log = Logger('sl4f_client');

/// Handles the SL4F server and communication with it.
class Sl4f {
  final _client = http.Client();

  static const _sshUser = 'fuchsia';
  static const _sl4fComponentUrl =
      'fuchsia-pkg://fuchsia.com/sl4f#meta/sl4f.cmx';
  static const _sl4fComponentName = 'sl4f.cmx';

  /// Authority (IP, hostname, etc.) of the device under test.
  final String target;

  /// Path to an SSH key file. For in-tree Fuchsia development, this can be
  /// the resolved path of `//.ssh/pkey`.
  final String sshKeyPath;

  /// This lives here for a soft API transition and should be removed in the future.
  Inspect _inspect;

  Sl4f(this.target, this.sshKeyPath)
      : assert(target != null && target.isNotEmpty),
        assert(sshKeyPath != null && sshKeyPath.isNotEmpty) {
    _inspect = Inspect(sshProcess);
    _log..info('Target device: $target')..info('SSH key path: $sshKeyPath');
  }

  /// Constructs an SL4F client from the `FUCHSIA_IPV4_ADDR` and
  /// `FUCHSIA_SSH_KEY` environment variables.
  Sl4f.fromEnvironment()
      : this(Platform.environment['FUCHSIA_IPV4_ADDR'],
            Platform.environment['FUCHSIA_SSH_KEY']);

  /// Closes the underlying HTTP client.
  ///
  /// If clients remain unclosed, the dart process might not terminate.
  void close() {
    _client.close();
  }

  /// Sends a JSON-RPC request to SL4F.
  ///
  /// Throws a [JsonRpcException] if the SL4F server replied with a non-null
  /// error string.
  Future<dynamic> request(String method, [dynamic params]) async {
    // Although params is optional, this will pass a null params if it is
    // omitted. This is actually required by our SL4F server (although it is
    // not required in JSON RPC:
    // https://www.jsonrpc.org/specification#request_object).
    final httpRequest = http.Request('GET', Uri.http(target, ''))
      ..body = jsonEncode({'id': '', 'method': method, 'params': params});

    final httpResponse =
        await http.Response.fromStream(await _client.send(httpRequest));
    Map<String, dynamic> response = jsonDecode(httpResponse.body);
    final dynamic error = response['error'];
    if (error != null) {
      throw JsonRpcException(httpRequest.body, error);
    }

    return response['result'];
  }

  /// Starts the SL4F server on the target using ssh.
  ///
  /// It will attempt to connect [tries] times, waiting at least [delay]
  /// between each attempt.
  /// Throws a [Sl4fException] if after this SL4F failed to start.
  Future<void> startServer(
      {int tries = 150, Duration delay = const Duration(seconds: 1)}) async {
    if (tries <= 0) {
      throw ArgumentError.value(tries, 'tries', 'Must be a positive integer');
    }

    for (var attempt = 0; attempt < tries; attempt++) {
      if (attempt > 0) {
        // TODO(isma): We should limit the wait time to as much delay.
        await Future.delayed(delay);
      }

      // Check if it's already started.
      if (await _isRunning()) {
        _log.info('SL4F has started.');
        return;
      }

      _log.info('Try $attempt at starting sl4f.');
      // 'run -d' doesn't exit with error if the component doesn't exist
      // (CF-666). Since we'd like to have clear logs about that particular
      // error, we don't run with '-d', but that in turn means that the ssh
      // command will be waiting for sl4f to exit. So we rely only on
      // [_isRunning] to tell whether sl4f has started successfully.
      // ignore: unawaited_futures
      ssh('run $_sl4fComponentUrl');

      if (await _isRunning(tries: 3, delay: Duration(seconds: 2))) {
        _log.info('SL4F has started.');
        return;
      }
    }
    throw Sl4fException('Sl4f has not started.');
  }

  /// SSHs into the device and starts the SL4F server.
  ///
  /// If no ssh key path is given, it's taken from the FUCHSIA_SSH_KEY env var.
  Future<void> stopServer() async {
    if (!await ssh('killall $_sl4fComponentName')) {
      _log.warning('Could not stop sl4f. Continuing.');
    }
  }

  /// Starts an ssh [Process], sending [cmd] to the target using ssh.
  Future<Process> sshProcess(String cmd,
      {ProcessStartMode mode = ProcessStartMode.normal}) {
    _log.fine('Running over ssh: $cmd');
    return Process.start(
        'ssh',
        [
          // Private key file path.
          '-i', sshKeyPath,
          // Don't check known_hosts.
          '-o', 'UserKnownHostsFile=/dev/null',
          // Auto add the fingerprint of remote host.
          '-o', 'StrictHostKeyChecking=no',
          // Timeout to connect, keeping it short makes the logs more sensical.
          '-o', 'ConnectTimeout=2',
          // These five arguments allow ssh to reuse its connection.
          '-o', 'ControlPersist=yes',
          '-o', 'ControlMaster=auto',
          '-o', 'ControlPath=/tmp/fuchsia--%r@%h:%p',
          '-o', 'ServerAliveInterval=1',
          '-o', 'ServerAliveCountMax=1',
          // These two arguments determine the connection timeout,
          // in the case the ssh connection gets lost.
          // They say if the target doesn't respond within 10 seconds, six
          // times in a row, terminate the connection.
          '-o', 'ServerAliveInterval=10',
          '-o', 'ServerAliveCountMax=6',
          '$_sshUser@$target',
          cmd
        ],
        // If not run in a shell it doesn't seem like the PATH is searched.
        runInShell: true,
        mode: mode);
  }

  /// Runs the command given by [cmd] on the target using ssh.
  ///
  /// It can optionally send input via [stdin].
  Future<bool> ssh(String cmd, {String stdin}) async {
    final process = await sshProcess(cmd);
    if (stdin != null) {
      process.stdin.write(stdin);
      await process.stdin.flush();
      await process.stdin.close();
    }
    if (await process.exitCode != 0) {
      _log
        ..warning(await process.stdout.transform(utf8.decoder).join())
        ..warning(await process.stderr.transform(utf8.decoder).join());
      return false;
    }

    // We must read all data otherwise the program might hang.
    await Future.wait([
      process.stdin.close(),
      process.stdout.drain(),
      process.stderr.drain()
    ]);

    return true;
  }

  /// Obtains the root inspect object for a component whose path includes
  /// [componentName].
  Future<dynamic> inspectComponentRoot(String componentName) async {
    return await _inspect.inspectComponentRoot(componentName);
  }

  /// Retrieves the inpect node(s) of [hubEntries], recursively, as a json object.
  Future<dynamic> inspectRecursively(String hubEntries) async {
    return await _inspect.inspectRecursively(hubEntries);
  }

  /// Retrieves a list of hub entries.
  ///
  /// If [filter] is set, only those entries containing [filter] are returned.
  Future<List<String>> retrieveHubEntries({String filter}) async {
    return await _inspect.retrieveHubEntries(filter: filter);
  }

  /// Restarts the device under test.
  ///
  /// Throws an Sl4fException if it fails to reboot the device or if all the
  /// attempts at restarting SL4F fail.
  Future<void> reboot() async {
    _log.info('Initiating reboot sequence.');
    // Kill SL4F first, we'll use it to try to guess when the reboot is done.
    await stopServer();
    // Issue a reboot command and wait.
    if (!await ssh('dm reboot')) {
      throw Sl4fException('Failed rebooting device.');
    }
    await Future.delayed(Duration(seconds: 20));

    // Try to restart SL4F
    return startServer();
  }

  /// Sends an empty http request to the server to verify if it's listening on
  /// port 80.
  ///
  /// By default it tries to connect just once, but that can be changed with
  /// [tries]. In which case it will wait [delay] time between tries.
  /// The server must respond within [timeout] before its considered to be
  /// unreachable.
  Future<bool> _isRunning(
      {int tries = 1,
      Duration delay = const Duration(seconds: 2),
      Duration timeout = const Duration(seconds: 10)}) async {
    if (tries <= 0) {
      throw ArgumentError.value(tries, 'tries', 'Must be a positive integer');
    }
    // TODO(isma): We should refactor the retry logic into its own higher-order
    // function.
    for (var attempt = 0; attempt < tries; attempt++) {
      if (attempt > 0) {
        await Future.delayed(delay);
      }

      try {
        await http.get(Uri.http(target, '/')).timeout(timeout);
      } on SocketException {
        continue;
      } on TimeoutException {
        continue;
      } on http.ClientException {
        continue;
      }
      return true;
    }
    return false;
  }
}
