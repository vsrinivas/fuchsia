// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is named sl4f_client.dart since the top-level dart file is names
// sl4f.dart, which would cause problems for src files trying to import this
// file.

import 'dart:async';
import 'dart:convert';
import 'dart:io' show Platform, Process, SocketException;

import 'package:http/http.dart' as http;

import 'exceptions.dart';

/// Handles the SL4F server and communication with it.
class Sl4f {
  final _client = http.Client();

  static const _sshUser = 'fuchsia';
  static const _sl4fComponentUrl =
      'fuchsia-pkg://fuchsia.com/sl4f#meta/sl4f.cmx';
  static const _sl4fComponentName = 'sl4f.cmx';

  /// Hostname of the device under test.
  String host;

  Sl4f(this.host) {
    print('Target host: $host');
  }

  /// Constructs an SL4F client from the FUCHSIA_IPV4_ADDR environment variable.
  Sl4f.fromEnvironment() : this(Platform.environment['FUCHSIA_IPV4_ADDR']);

  /// Closes the underlying HTTP client.
  ///
  /// If clients remain unclosed, the dart process might not terminate.
  void close() {
    _client.close();
  }

  /// Sends a JSON-RPC request to SL4F.
  Future<dynamic> request(String method, [dynamic params]) async {
    // Although params is optional, this will pass a null params if it is
    // omitted. This is actually required by our SL4F server (although it is
    // not required in JSON RPC:
    // https://www.jsonrpc.org/specification#request_object).
    final httpRequest = http.Request('GET', Uri.http(host, ''))
      ..body = jsonEncode({'id': '', 'method': method, 'params': params});

    final httpResponse =
        await http.Response.fromStream(await _client.send(httpRequest));
    Map<String, dynamic> response = jsonDecode(httpResponse.body);
    final dynamic error = response['error'];
    if (error != null) {
      throw JsonRpcException(error);
    }

    return response['result'];
  }

  /// Starts the SL4F server on the host using ssh.
  ///
  /// If no [sshKeyPath] is given, it's taken from the FUCHSIA_SSH_KEY env var.
  /// It will attempt to connect [tries] times, waiting at least [delay]
  /// between each attempt.
  /// Throws a StateError if after this is ran SL4F has not started.
  Future<void> startServer(
      {String sshKeyPath,
      int tries = 5,
      Duration delay = const Duration(seconds: 30)}) async {
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
        print('SL4F has started.');
        return;
      }

      print('Try $attempt at starting sl4f.');
      // 'run -d' doesn't exit with error if the component doesn't exist
      // (CF-666). Since we'd like to have clear logs about that particular
      // error, we don't run with '-d', but that in turn means that the ssh
      // command will be waiting for sl4f to exit. So we rely only on
      // [_isRunning] to tell whether sl4f has started successfully.
      // ignore: unawaited_futures
      _runSshCommand('run $_sl4fComponentUrl', sshKeyPath: sshKeyPath);

      if (await _isRunning(tries: 3, delay: Duration(seconds: 2))) {
        print('SL4F has started.');
        return;
      }
    }
    throw Sl4fException('Sl4f has not started.');
  }

  /// SSHs into the device and starts the SL4F server.
  ///
  /// If no ssh key path is given, it's taken from the FUCHSIA_SSH_KEY env var.
  Future<void> stopServer({String sshKeyPath}) async {
    if (!await _runSshCommand('killall $_sl4fComponentName',
        sshKeyPath: sshKeyPath)) {
      print('Could not stop sl4f. Continuing.');
    }
  }

  /// Runs the command given by [cmd] on the host using ssh.
  Future<bool> _runSshCommand(String cmd, {String sshKeyPath}) async {
    sshKeyPath ??= Platform.environment['FUCHSIA_SSH_KEY'];

    print('Running over ssh: $cmd');
    final result = await Process.run(
        'ssh',
        [
          '-i',
          sshKeyPath,
          '-o',
          'UserKnownHostsFile=/dev/null',
          '-o',
          'StrictHostKeyChecking=no',
          '$_sshUser@$host',
          cmd
        ],
        // If not run in a shell it doesn't seem like the PATH is searched.
        runInShell: true);
    if (result.exitCode != 0) {
      print(result.stdout);
      print(result.stderr);
      return false;
    }
    return true;
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
        await http.get(Uri.http(host, '/')).timeout(timeout);
      } on SocketException {
        continue;
      } on TimeoutException {
        continue;
      }
      return true;
    }
    return false;
  }
}
