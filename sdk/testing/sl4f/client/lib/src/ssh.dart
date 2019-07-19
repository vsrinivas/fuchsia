// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:logging/logging.dart';

class Ssh {
  static const _sshUser = 'fuchsia';

  final _log = Logger('ssh');

  /// Authority (IP, hostname, etc.) of the device under test.
  final String target;

  /// Path to an SSH key file. For in-tree Fuchsia development, this can be
  /// the resolved path of `//.ssh/pkey`.
  final String sshKeyPath;

  Ssh(this.target, this.sshKeyPath)
      : assert(target != null && target.isNotEmpty),
        assert(sshKeyPath != null && sshKeyPath.isNotEmpty) {
    _log.info('SSH key path: $sshKeyPath');
  }

  /// Starts an ssh [Process], sending [cmd] to the target using ssh.
  Future<Process> start(String cmd,
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
  /// It can optionally send input via [stdin]. If the exit code is nonzero, diagnostic warnings
  /// are logged.
  Future<ProcessResult> run(String cmd, {String stdin}) async {
    final process = await start(cmd);
    if (stdin != null) {
      process.stdin.write(stdin);
      await process.stdin.flush();
    }
    await process.stdin.close();

    final result = ProcessResult(
        process.pid,
        await process.exitCode,
        await systemEncoding.decodeStream(process.stdout),
        await systemEncoding.decodeStream(process.stderr));

    if (result.exitCode != 0) {
      _log
        ..warning('$cmd; exit code: ${result.exitCode}')
        ..warning(result.stdout)
        ..warning(result.stderr);
    }

    return result;
  }
}
