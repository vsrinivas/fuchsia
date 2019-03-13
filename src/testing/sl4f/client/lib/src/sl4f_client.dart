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

/// Wraps the error field of a JSON RPC as an [Exception].
class JsonRpcException implements Exception {
  JsonRpcException(this.error);
  final dynamic error;

  @override
  String toString() {
    return 'JSON RPC returned error: $error';
  }
}

/// Any exception when dealing with the SL4F server itself.
class Sl4fException implements Exception {
  String error;
  Sl4fException(this.error);
}

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

  /// Closes the underlying HTTP client. This need not be called if the
  /// Sl4f client is closed instead.
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
  /// If no ssh key path is given, it's taken from the FUCHSIA_SSH_KEY env var.
  /// It returns throws a StateError if after this is ran SL4F has not started.
  Future<void> startServer({String sshKeyPath}) async {
    if (await _isRunning()) {
      return;
    }

    final result = await _runSshCommand('run -d $_sl4fComponentUrl',
        sshKeyPath: sshKeyPath);
    if (!result) {
      // TODO(isma): We should use a better exception type.
      throw Sl4fException('Sl4f has not started.');
    }

    for (var i = 0; i < 3; i++) {
      await Future.delayed(Duration(seconds: 2));
      if (await _isRunning()) {
        print('SL4F has started.');
        return;
      }
    }
    throw Sl4fException('Sl4f has not started.');
  }

  /// SSHs into the device and starts the SL4F server.
  ///
  /// If no ssh key path is given, it's taken from the FUCHSIA_SSH_KEY env var.
  /// It returns false if it failed to call killall on the device.
  Future<void> stopServer({String sshKeyPath}) async {
    if (!await _runSshCommand('killall $_sl4fComponentName',
        sshKeyPath: sshKeyPath)) {
      throw Sl4fException('Could not stop Sl4f.');
    }
  }

  /// Runs the given ssh command on the host.
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
        // If not ran in shell it doesn't seem like the PATH is searched.
        runInShell: true);
    if (result.exitCode != 0) {
      print(result.stderr);
      return false;
    }
    return true;
  }

  /// Sends an empty http request to the server to verify if it's Listening on
  /// port 80.
  Future<bool> _isRunning() async {
    try {
      await http.get(Uri.http(host, '/')).timeout(Duration(seconds: 10));
    } on SocketException {
      return false;
    } on TimeoutException {
      return false;
    }
    return true;
  }
}
