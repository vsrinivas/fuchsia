// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:logging/logging.dart';
import 'package:ports/ports.dart';
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
