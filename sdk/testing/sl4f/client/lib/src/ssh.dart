// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:logging/logging.dart';
import 'package:meta/meta.dart';
import 'package:retry/retry.dart';

import 'exceptions.dart';

/// Allows access to the device via SSH.
///
/// This class is used by [Sl4f] to start the SL4F server if it isn't running,
/// but it can also be used to run arbitrary shell commands on the device, and
/// to forward ports available only within the device.
///
/// Note that in general SSH and CLI shells are a hack on top of Fuchsia which
/// doesn't really fit our process and interaction model. Before using [Ssh]
/// to run an arbitrary command, look at the other SL4F Client classes for
/// alternatives which often don't need SSH. For example:
///
///  * Instead of `run` use [Component.launch] or [Modular.startBasemgr].
///  * Instead of `cs` to see running component names use [Component.list].
///  * Instead of `sessionctl` look at the [Modular] class.
///  * Instead of `trace` use the [Performance] class.
class Ssh {
  static const _sshUser = 'fuchsia';

  final _log = Logger('ssh');

  /// Authority (IP, hostname, etc.) of the device under test.
  final String target;

  /// SSH port to connect to the device under test, or null for default port
  final int sshPort;

  /// Path to an SSH key file. For in-tree Fuchsia development, this can be
  /// the resolved path of `//.ssh/pkey`.
  final String sshKeyPath;

  /// Builds an SSH object that uses the credentials from a file.
  Ssh(this.target, this.sshKeyPath, [this.sshPort])
      : assert(target != null && target.isNotEmpty),
        assert(sshKeyPath != null && sshKeyPath.isNotEmpty),
        assert(sshPort == null || sshPort > 0) {
    _log.info('SSH key path: $sshKeyPath, setting owner only');
    // Swarming does not maintain file permissions any longer, so this file will
    // be world-readable when it arrives on the tester bot. Ensure that it's not
    // readable otherwise ssh will reject it. See https://fxbug.dev/53492 and
    // https://crbug.com/1092020.
    Process.run('chmod', ['og-rwx', sshKeyPath], runInShell: true);
  }

  /// Builds an SSH object that uses the credentials from ssh-agent only.
  Ssh.useAgent(this.target, [this.sshPort])
      : assert(target != null && target.isNotEmpty),
        assert(sshPort == null || sshPort > 0),
        sshKeyPath = null;

  /// Starts an ssh [Process], sending [cmd] to the target using ssh.
  Future<Process> start(String cmd,
      {ProcessStartMode mode = ProcessStartMode.normal}) {
    _log.fine('Running over ssh: $cmd');

    return Process.start('ssh', makeArgs(cmd),
        // If not run in a shell it doesn't seem like the PATH is searched.
        runInShell: true,
        mode: mode);
  }

  /// Runs the command given by [cmd] on the target using ssh.
  ///
  /// It can optionally send input via [stdin], and can optionally incrementally
  /// emit output via [stdoutConsumer] and [stderrConsumer].
  ///
  /// If the exit code is nonzero, diagnostic warnings are logged.
  Future<ProcessResult> runWithOutput(String cmd,
      {String stdin,
      StreamConsumer<String> stdoutConsumer,
      StreamConsumer<String> stderrConsumer}) async {
    final process = await start(cmd);

    if (stdin != null) {
      process.stdin.write(stdin);
    }

    final localStdoutAll = StringBuffer();
    final localStderrAll = StringBuffer();

    final localStdoutStream =
        process.stdout.transform(systemEncoding.decoder).map((String data) {
      localStdoutAll.write(data);
      return data;
    });
    final localStderrStream =
        process.stderr.transform(systemEncoding.decoder).map((String data) {
      localStderrAll.write(data);
      return data;
    });

    // Note: The pipe and drain calls here create StreamSubscription objects
    // that will only cancel when the corresponding stream closes. Since this
    // function always waits for process to finish, we can rely on the stdout
    // and stderr streams closing. If you are adding any code path that tries to
    // kill the ssh process (like a timeout), these calls should be replaced
    // with an equivalent that gives access to the underlying StreamSubscription
    // so that it can be cancelled in the timeout as well, otherwise the dart
    // process may hang after finishing its work due to the subscriptions being
    // open.
    final Future<void> stdoutFuture = (stdoutConsumer != null)
        ? localStdoutStream.pipe(stdoutConsumer)
        : localStdoutStream.drain();
    final Future<void> stderrFuture = (stderrConsumer != null)
        ? localStderrStream.pipe(stderrConsumer)
        : localStderrStream.drain();

    Future<void> flushAndCloseStdin() async {
      // These two need to be sequenced in order.
      await process.stdin.flush();
      await process.stdin.close();
    }

    // Dart futures are run regardless of being awaited or not, so despite
    // waiting sequentially here stdout and stderr are read from concurrently.
    await flushAndCloseStdin();
    await stdoutFuture;
    await stderrFuture;

    final result = ProcessResult(
      process.pid,
      await process.exitCode,
      localStdoutAll.toString(),
      localStderrAll.toString(),
    );

    if (result.exitCode != 0) {
      _log
        ..warning('$cmd; exit code: ${result.exitCode}')
        ..warning(result.stdout)
        ..warning(result.stderr);
    }

    return result;
  }

  /// Runs the command given by [cmd] on the target using ssh.
  ///
  /// It can optionally send input via [stdin]. If the exit code is nonzero,
  /// diagnostic warnings are logged.
  Future<ProcessResult> run(String cmd, {String stdin}) =>
      runWithOutput(cmd, stdin: stdin);

  /// Forwards TCP connections from the local [port] to the DUT's [remotePort].
  ///
  /// If [port] is not provided, an unused port will be allocated.
  /// The return value is the local forwarded port, or [PortForwardException] is
  /// thrown in case of error.
  Future<int> forwardPort(
      {@required int remotePort, int port, int tries = 5}) async {
    port ??= await pickUnusedPort();
    _log.fine('Forwarding TCP port: localhost:$port -> $target:$remotePort');
    await retry(
      () => _forwardPort(remotePort, port),
      retryIf: (e) => e is PortForwardException,
      maxAttempts: tries,
    );
    return port;
  }

  /// Forwards a port to the DUT without retries.
  Future<void> _forwardPort(int remotePort, int port) async {
    final result = await Process.run(
        'ssh', makeForwardArgs(port, remotePort, cancel: false),
        runInShell: true);
    if (result.exitCode != 0) {
      throw PortForwardException(
          'localhost:$port',
          '$target:$remotePort',
          'Failed to initiate Port Forward. '
              'STDOUT: "${result.stdout}". STDERR: "${result.stderr}".');
    }
  }

  /// Cancels a TCP port forward.
  ///
  /// Completes to PortForwardException in case of failure.
  Future<void> cancelPortForward(
      {@required int remotePort, @required int port}) async {
    _log.fine('Canceling TCP port forward: '
        'localhost:$port -> $target:$remotePort');

    final result = await Process.run(
        'ssh', makeForwardArgs(port, remotePort, cancel: true),
        runInShell: true);
    if (result.exitCode != 0) {
      throw PortForwardException(
          'localhost:$port',
          '$target:$remotePort',
          'Failed to cancel Port Forward. '
              'STDOUT: "${result.stdout}". STDERR: "${result.stderr}".');
    }
  }

  /// Forwards TCP connections from the DUT's [remotePort] to the local [port].
  ///
  /// [PortForwardException] is thrown in case of error.
  Future<int> forwardRemotePort(
      {@required int remotePort, @required int port, int tries = 5}) async {
    _log.fine('Forwarding TCP port: $target:$remotePort -> localhost:$port');
    await retry(
      () => _forwardRemotePort(remotePort, port),
      retryIf: (e) => e is PortForwardException,
      maxAttempts: tries,
    );
    return port;
  }

  /// Forwards a port from the DUT to the host without retries.
  Future<void> _forwardRemotePort(int remotePort, int port) async {
    final result = await Process.run(
        'ssh', makeRemoteForwardArgs(remotePort, port, cancel: false),
        runInShell: true);
    if (result.exitCode != 0) {
      throw PortForwardException(
          '$target:$remotePort',
          'localhost:$port',
          'Failed to initiate Remote Port Forward. '
              'STDOUT: "${result.stdout}". STDERR: "${result.stderr}".');
    }
  }

  /// Cancels a TCP remote port forward.
  ///
  /// Completes to PortForwardException in case of failure.
  Future<void> cancelRemotePortForward(
      {@required int remotePort, @required int port}) async {
    _log.fine('Canceling TCP port forward: '
        '$target:$remotePort -> localhost:$port');

    final result = await Process.run(
        'ssh', makeRemoteForwardArgs(remotePort, port, cancel: true),
        runInShell: true);
    if (result.exitCode != 0) {
      throw PortForwardException(
          '$target:$remotePort',
          'localhost:$port',
          'Failed to cancel Remote Port Forward. '
              'STDOUT: "${result.stdout}". STDERR: "${result.stderr}".');
    }
  }

  /// Returns a list of arguments for ssh (and other ssh-like tools) containing
  /// the default configuration options (i.e. -o arguments), as well as the path
  /// to the ssh key if it's configured. The list does not contain the target's
  /// user or host.
  List<String> get defaultArguments =>
      [
        // Don't check known_hosts.
        '-o', 'UserKnownHostsFile=/dev/null',
        // Auto add the fingerprint of remote host.
        '-o', 'StrictHostKeyChecking=no',
        // Timeout to connect, short so the logs can make sense.
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
      ] +
      (sshKeyPath != null ? ['-i', sshKeyPath] : []) +
      (sshPort != null && sshPort != 0 ? ['-p', sshPort.toString()] : []);

  List<String> _makeBaseArgs() => defaultArguments + ['$_sshUser@$target'];

  @visibleForTesting
  List<String> makeArgs(String cmd) => _makeBaseArgs() + [cmd];

  @visibleForTesting
  List<String> makeForwardArgs(int localPort, int remotePort,
          {bool cancel = false}) =>
      _makeBaseArgs() +
      [
        // Do Not run a command.
        '-N',
        // TCP port forward from local to remote.
        '-L', 'localhost:$localPort:localhost:$remotePort',
        // Forwarding with -O makes sure we are reusing the same connection.
        '-O', cancel ? 'cancel' : 'forward',
      ];

  @visibleForTesting
  List<String> makeRemoteForwardArgs(int remotePort, int localPort,
          {bool cancel = false}) =>
      _makeBaseArgs() +
      [
        // Do Not run a command.
        '-N',
        // TCP port forward from remote to local.
        '-R', 'localhost:$remotePort:localhost:$localPort',
        // Forwarding with -O makes sure we are reusing the same connection.
        '-O', cancel ? 'cancel' : 'forward',
      ];

  /// Finds and returns an unused port on the test host in the local port range
  /// (see ip(7)).
  Future<int> pickUnusedPort() async {
    // Use bind to allocate an unused port, then unbind from that port to
    // make it available for use.
    final socket = await ServerSocket.bind('localhost', 0);
    final port = socket.port;
    await socket.close();

    return port;
  }
}
