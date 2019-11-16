// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is named sl4f_client.dart since the top-level dart file is names
// sl4f.dart, which would cause problems for src files trying to import this
// file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:http/http.dart' as http;
import 'package:logging/logging.dart';
import 'package:pedantic/pedantic.dart';

import 'dump.dart';
import 'exceptions.dart';
import 'ssh.dart';

final _log = Logger('sl4f_client');

/// Diagnostics have been known to hang in some failure cases. In those cases, log
/// and move on.
const _diagnosticTimeout = Duration(minutes: 2);

bool _isNullOrEmpty(String str) => str == null || str.isEmpty;

/// Handles the SL4F server and communication with it.
class Sl4f {
  static const diagnostics = {
    'iquery': 'iquery --report',
    'kstats': 'kstats -c -m -n 1',
    'net-if': 'net if list',
    'ps': 'ps -T',
    'top': 'top -n 1',
    'wlan': 'wlan status',
  };
  static const _sl4fComponentUrl =
      'fuchsia-pkg://fuchsia.com/sl4f#meta/sl4f.cmx';
  static const _sl4fComponentName = 'sl4f.cmx';

  final _client = http.Client();

  /// Authority (IP, hostname, etc.) of the device under test.
  final String target;
  final Ssh ssh;

  Sl4f(this.target, this.ssh) : assert(target != null && target.isNotEmpty) {
    _log.info('Target device: $target');
  }

  /// Constructs an SL4F client from the `FUCHSIA_IPV4_ADDR` and
  /// `FUCHSIA_SSH_KEY` environment variables.
  ///
  /// If `FUCHSIA_SSH_KEY` is not set but `SSH_AUTH_SOCK` is, then it's
  /// assumed that ssh-agent can provide the credentials to connect to the
  /// device. Otherwise an [Sl4fException] is thrown.
  factory Sl4f.fromEnvironment({Map<String, String> environment}) {
    environment ??= Platform.environment;
    final address = environment['FUCHSIA_IPV4_ADDR'];
    if (_isNullOrEmpty(address)) {
      throw Sl4fException(
          'No FUCHSIA_IPV4_ADDR provided when starting SL4F from env');
    }

    Ssh ssh;
    if (!_isNullOrEmpty(environment['FUCHSIA_SSH_KEY'])) {
      ssh = Ssh(address, environment['FUCHSIA_SSH_KEY']);
    } else if (!_isNullOrEmpty(environment['SSH_AUTH_SOCK'])) {
      ssh = Ssh.useAgent(address);
    } else {
      throw Sl4fException(
          'No FUCHSIA_SSH_KEY provided and SSH_AUTH_SOCK is not defined. '
          'Cannot start sl4f.');
    }

    String host = address;
    // This same code exists in the dart/sdk/lib/_http/http_impl.dart.
    if (host.contains(':')) {
      host = '[$host]';
    }
    return Sl4f(host, ssh);
  }

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
      // We run sl4f with `-d` to make sure that it keeps running even
      // if sshd dies or the connection somehow breaks.
      //
      // We apparently cannot rely on the ssh connection to stay open
      // indefinitely for as long as sl4f is running.
      //
      // This has the consequence that we won't get an error in the logs
      // if sl4f.cmx isn't available. This shouldn't be an issue if the
      // users start the test with the given instructions, especially
      // using the `end_to_end_deps` bundle, but it could still happen
      // if something gets misconfigured in the product build config.
      //
      // TODO(mesch): It seems as if we could just await this ssh()
      // call, but if we do this, we hang. Observed in
      // screen_navigation_test on workstation.
      unawaited(ssh.run('run -d $_sl4fComponentUrl'));

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
    if ((await ssh.run('killall $_sl4fComponentName')).exitCode != 0) {
      _log.warning('Could not stop sl4f. Continuing.');
    }
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
    // TODO(DNO-621): trap errors
    await ssh.run('dm reboot');
    await Future.delayed(Duration(seconds: 20));

    // Try to restart SL4F
    return startServer();
  }

  /// Dumps files with useful device diagnostics.
  Future<void> dumpDiagnostics(String dumpName, {Dump dump}) async {
    final dumper = dump ?? Dump();
    if (dumper.hasDumpDirectory) {
      await Future.wait(diagnostics.entries.map((diag) => _dumpDiagnostic(
          diag.value, '$dumpName-diagnostic-${diag.key}', dumper)));
    }
  }

  Future _dumpDiagnostic(String cmd, String dumpName, Dump dump) async {
    final proc = await ssh.start(cmd);
    unawaited(proc.stdin.close());

    // Pipe stdout directly to the file sink (it gets closed when done).
    final sink = dump.openForWrite(dumpName, 'txt');
    final dumpFuture = proc.stdout.pipe(sink);

    // Start decoding the stderr stream to ensure something consumes it,
    // otherwise it could cause dart to hang waiting for it to be consumed.
    final stderr = systemEncoding.decodeStream(proc.stderr);

    // Print something about the process in case it fails.
    Future<void> exitCode() async {
      final code = await proc.exitCode;
      if (code != 0) {
        _log
          ..warning('$cmd; exit code: $code')
          ..warning('stderr: ${await stderr}');
      }
    }

    // Await for all three of the above at the same time, this ensures that the
    // process outputs get consumed.
    return Future.wait([
      dumpFuture,
      stderr,
      // If the process takes too long we kill it. Note that because futures are
      // not cancellable, the check in [exitCode] still executes.
      exitCode(),
    ]).timeout(_diagnosticTimeout, onTimeout: () {
      _log.warning('$cmd; did not complete after $_diagnosticTimeout');
      proc.kill(ProcessSignal.sigkill);
      return [];
    });
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
