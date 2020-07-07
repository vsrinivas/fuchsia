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

/// Make sure there's only one instance of the item in the input.
///
/// Example expected input:
/// Rule {
///     host_match: "fuchsia.com",
///     host_replacement: "devhost",
///     path_prefix_match: "/",
///     path_prefix_replacement: "/",
/// }
///
/// We want to make sure there is only one instance of a given key:value.
bool hasExclusivelyOneItem(
    String input, String expectedKey, String expectedValue) {
  return 1 ==
      RegExp('($expectedKey):\\s+\\"($expectedValue)\\"')
          .allMatches(input)
          .length;
}

/// Find the redirect rule for `fuchsia.com`, if there is one.
///
/// Example expected input:
/// Rule {
///     host_match: "fuchsia.com",
///     host_replacement: "devhost",
///     path_prefix_match: "/",
///     path_prefix_replacement: "/",
/// }
///
/// Returns `"devhost"` for the above example input.
/// Returns `null` if there are no rules for `"fuchsia.com"`.
String getCurrentRewriteRule(String ruleList) {
  String fuchsiaRule = '';
  var matches = RegExp('Rule (\{.+?\})', dotAll: true).allMatches(ruleList);
  for (final match in matches) {
    if (hasExclusivelyOneItem(match.group(0), 'host_match', 'fuchsia.com')) {
      fuchsiaRule = match.group(0);
      break;
    }
  }
  matches = RegExp('host_replacement:\\s+\\"(.+)\\"').allMatches(fuchsiaRule);
  for (final match in matches) {
    // There should only be one match. If not, just take the first one.
    return match.group(1);
  }
  return null;
}

void main() {
  final log = Logger('package_manager_test');
  final pmPath = Platform.script.resolve('runtime_deps/pm').toFilePath();

  sl4f.Sl4f sl4fDriver;
  Process serveProcess;

  setUpAll(() async {
    Logger.root
      ..level = Level.ALL
      ..onRecord.listen((rec) => print('[${rec.level}]: ${rec.message}'));
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
  });

  tearDownAll(() async {
    serveProcess.kill();
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });
  group('Package Manager', () {
    Directory tempDir;
    setUp(() async {
      tempDir = await Directory.systemTemp.createTemp('repo');
    });
    tearDown(() async {
      // TODO(vfcc): Write generic logic to observe the state of the repos and restore them to
      // expected defaults.
      tempDir.deleteSync(recursive: true);
    });
    test(
        'Test that creates a repository, deploys a package, and '
        'validates that the deployed package is visible from the server',
        () async {
      // Covers these commands (success cases only):
      // amberctl add_src -n <path> -f http://<host>:<port>/config.json
      // amberctl enable_src -n devhost
      // amberctl rm_src -n <name>
      // pkgctl repo
      // pm serve -repo=<path> -l :<port>
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

      // Typically, there is a pre-existing rule pointing to `devhost`, but it isn't
      // guaranteed. Record what the rule list is before we begin, and confirm that is
      // the rule list when we are finished.

      log.info('Recording the current rule list');
      var ruleListResponse = await sl4fDriver.ssh.run('pkgctl rule list');
      expect(ruleListResponse.exitCode, 0);
      final originalRuleList = ruleListResponse.stdout.toString();

      var originalRewriteRule = getCurrentRewriteRule(originalRuleList);

      log.info(
          'Adding the new repository as an update source with http://$hostAddress:$port');
      final addRepoCfgResponse = await sl4fDriver.ssh.run(
          'amberctl add_src -n $repoPath -f http://$hostAddress:$port/config.json');
      expect(addRepoCfgResponse.exitCode, 0);

      log.info('Running pkgctl repo to list sources');
      var listSrcsResponse = await sl4fDriver.ssh.run('pkgctl repo');
      expect(listSrcsResponse.exitCode, 0);

      var listSrcsResponseOutput = listSrcsResponse.stdout.toString();
      String repoName = repoPath.replaceAll('/', '_');
      String repoUrl = 'fuchsia-pkg://$repoName';

      expect(listSrcsResponseOutput.contains(repoUrl), isTrue);

      log.info('Confirm rule list points to $repoName');
      ruleListResponse = await sl4fDriver.ssh.run('pkgctl rule list');
      expect(ruleListResponse.exitCode, 0);

      var ruleListResponseOutput = ruleListResponse.stdout.toString();
      expect(
          hasExclusivelyOneItem(
              ruleListResponseOutput, 'host_replacement', repoName),
          isTrue);

      log.info('Cleaning up temp repo');
      final rmSrcResponse =
          await sl4fDriver.ssh.run('amberctl rm_src -n $repoName');
      expect(rmSrcResponse.exitCode, 0);

      log.info('Checking that $repoUrl is gone');
      listSrcsResponse = await sl4fDriver.ssh.run('pkgctl repo');
      expect(listSrcsResponse.exitCode, 0);

      listSrcsResponseOutput = listSrcsResponse.stdout.toString();
      expect(listSrcsResponseOutput.contains(repoUrl), isFalse);

      if (originalRewriteRule != null) {
        log.info('Re-enabling original repo source');
        final enableSrcResponse = await sl4fDriver.ssh
            .run('amberctl enable_src -n $originalRewriteRule');
        expect(enableSrcResponse.exitCode, 0);
      }

      log.info('Confirm rule list is back to its original set.');
      ruleListResponse = await sl4fDriver.ssh.run('pkgctl rule list');
      expect(ruleListResponse.exitCode, 0);

      ruleListResponseOutput = ruleListResponse.stdout.toString();
      expect(ruleListResponseOutput, originalRuleList);
    });
  }, timeout: _timeout);
}
