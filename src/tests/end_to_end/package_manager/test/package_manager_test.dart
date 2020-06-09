// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:logging/logging.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

// TODO(fxb/53292): update to use test size.
const _timeout = Timeout(Duration(minutes: 5));

Future<String> formattedHostAddress(sl4f.Sl4f sl4fDriver) async {
  final output = await sl4fDriver.ssh.run('echo \$SSH_CONNECTION');
  final hostAddress =
      output.stdout.toString().split(' ')[0].replaceAll('%', '%25');
  return '[$hostAddress]';
}

Future<ProcessResult> initializeRepo(String pmPath, String repoPath) async =>
    Process.run(pmPath, ['newrepo', '-repo=$repoPath']);

Future<ProcessResult> publishPackage(
        String pmPath, String repoPath, String archivePath) async =>
    Process.run(
        pmPath, ['publish', '-a', '-n', '-r=$repoPath', '-f=$archivePath']);

Future<ProcessResult> createArchive(
    String pmPath, String packageManifestPath) async {
  // Running the process from runtime_deps directory is needed since meta.far needs to be
  // a relative path.
  final workingDirectory = Platform.script.resolve('runtime_deps').toFilePath();
  return Process.run(pmPath, ['-m=$packageManifestPath', 'archive'],
      workingDirectory: workingDirectory);
}

// TODO(fxb/53300): move to cts/utils.
/// Repeatedly finds a probably-unused port on localhost and passes it to
/// [tryPort] until it binds successfully.
///
/// [tryPort] should return a non-`null` value or a Future completing to a
/// non-`null` value once it binds successfully. This value will be returned
/// by [getUnusedPort] in turn.
///
/// This is necessary for ensuring that our port binding isn't flaky for
/// applications that don't print out the bound port.
Future<T> getUnusedPort<T>(FutureOr<T> Function(int port) tryPort) async {
  T value;
  await Future.doWhile(() async {
    value = await tryPort(await getUnsafeUnusedPort());
    return value == null;
  });
  return value;
}

/// This bool is intended to guard from needing 2 binds for every port that we need.
/// Once an IPv6 bind fails, we assume it won't ever work and use IPv4 only
/// from there on out.
var _maySupportIPv6 = true;

// TODO(fxb/53300): move to cts/utils.
/// Returns a port that is probably, but not definitely, not in use.
///
/// This has a built-in race condition: another process may bind this port at
/// any time after this call has returned. If at all possible, callers should
/// use [getUnusedPort] instead.
Future<int> getUnsafeUnusedPort() async {
  int port;
  if (_maySupportIPv6) {
    try {
      final socket = await RawServerSocket.bind(InternetAddress.loopbackIPv6, 0,
          v6Only: true);
      port = socket.port;
      await socket.close();
    } on SocketException {
      _maySupportIPv6 = false;
    }
  }
  if (!_maySupportIPv6) {
    final socket = await RawServerSocket.bind(InternetAddress.loopbackIPv4, 0);
    port = socket.port;
    await socket.close();
  }
  return port;
}

void main() {
  final log = Logger('package_manager_test');
  final pmPath = Platform.script.resolve('runtime_deps/pm').toFilePath();

  Directory tempDir;
  sl4f.Sl4f sl4fDriver;
  Process serveProcess;

  setUpAll(() async {
    Logger.root
      ..level = Level.ALL
      ..onRecord.listen((rec) => print('[${rec.level}]: ${rec.message}'));
    tempDir = await Directory.systemTemp.createTemp('repo');
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
  });

  tearDownAll(() async {
    tempDir.deleteSync(recursive: true);
    serveProcess.kill();
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });
  group('Package Manager', () {
    test(
        'Test that creates a repository, deploys a package, and '
        'validates that the deployed package is visible from the server',
        () async {
      final repoPath = tempDir.path;
      log.info('Initializing repo: $repoPath');
      final initializeRepoResponse = await initializeRepo(pmPath, repoPath);
      expect(initializeRepoResponse.exitCode, 0);

      final manifestPath = Platform.script
          .resolve('runtime_deps/package_manifest.json')
          .toFilePath();
      log.info('Creating archive from package_manifest');
      final createArchiveResponse = await createArchive(pmPath, manifestPath);
      expect(createArchiveResponse.exitCode, 0);

      final archivePath = Platform.script
          .resolve('runtime_deps/component_hello_world-0.far')
          .toFilePath();
      log.info(
          'Publishing package from archive: $archivePath to repo: $repoPath');
      final publishPackageResponse =
          await publishPackage(pmPath, repoPath, archivePath);
      expect(publishPackageResponse.exitCode, 0);

      int port;
      serveProcess = await getUnusedPort<Process>((unusedPort) {
        port = unusedPort;
        List<String> arguments = [
          'serve',
          '-repo=$repoPath',
          '-l',
          ':$unusedPort'
        ];
        log.info('Serve is starting on port: $unusedPort');
        return Process.start(pmPath, arguments);
      });

      log.info('Getting the available packages');
      final curlResponse =
          await Process.run('curl', ['http://localhost:$port/targets.json']);

      expect(curlResponse.exitCode, 0);
      final curlOutput = curlResponse.stdout.toString();
      expect(curlOutput.contains('component_hello_world/0'), isTrue);

      log.info('Getting Host Address');
      final hostAddress = await formattedHostAddress(sl4fDriver);

      log.info(
          'Adding the new repository as an update source with http://$hostAddress:$port');
      final addRepoCfgResponse = await sl4fDriver.ssh.run(
          'amberctl add_src -n $repoPath -f http://$hostAddress:$port/config.json');
      expect(addRepoCfgResponse.exitCode, 0);

      log.info('Running list_srcs');
      final listSrcsResponse = await sl4fDriver.ssh.run('amberctl list_srcs');
      expect(listSrcsResponse.exitCode, 0);

      final listSrcsResponseOutput = listSrcsResponse.stdout.toString();
      String repoUrl = repoPath.replaceAll('/', '_');
      repoUrl = 'fuchsia-pkg://$repoUrl';
      expect(listSrcsResponseOutput.contains(repoUrl), isTrue);
    });
  }, timeout: _timeout);
}
