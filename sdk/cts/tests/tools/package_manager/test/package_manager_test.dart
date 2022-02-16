// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:archive/archive.dart';
import 'package:archive/archive_io.dart';
import 'package:logging/logging.dart';
import 'package:path/path.dart' as path;
import 'package:pkg/pkg.dart';
import 'package:pm/pm.dart';
import 'package:retry/retry.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

// TODO(fxbug.dev/53292): update to use test size.
const _timeout = Timeout(Duration(minutes: 5));

void printErrorHelp() {
  print('If this test fails, see '
      'https://fuchsia.googlesource.com/a/fuchsia/+/HEAD/sdk/cts/tools/package_manager/README.md'
      ' for details!');
}

// validRepoName replaces invalid characters in the input sequence to ensure
// the returned string complies to
// https://fuchsia.dev/fuchsia-src/concepts/packages/package_url?hl=en#repository
String validRepoName(String originalName) {
  return originalName.replaceAll(RegExp(r'(?![a-z0-9-]).'), '-');
}

Future<String> formattedHostAddress(sl4f.Sl4f sl4fDriver) async {
  final output = await sl4fDriver.ssh.run('echo \$SSH_CONNECTION');
  final hostAddress =
      output.stdout.toString().split(' ')[0].replaceAll('%', '%25');
  return '[$hostAddress]';
}

void main() {
  final log = Logger('package_manager_test');
  final runtimeDepsPath = Platform.script.resolve('runtime_deps').toFilePath();
  final pmPath = Platform.script.resolve('runtime_deps/pm').toFilePath();
  String hostAddress;
  String manifestPath;

  sl4f.Sl4f sl4fDriver;

  setUpAll(() async {
    Logger.root
      ..level = Level.ALL
      ..onRecord.listen((rec) => print('[${rec.level}]: ${rec.message}'));
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();

    hostAddress = await formattedHostAddress(sl4fDriver);

    // Extract the `package.tar`.
    final packageTarPath =
        Platform.script.resolve('runtime_deps/package.tar').toFilePath();
    final bytes = File(packageTarPath).readAsBytesSync();
    final packageTar = TarDecoder().decodeBytes(bytes);
    for (final file in packageTar) {
      final filename = file.name;
      if (file.isFile) {
        List<int> data = file.content;
        File(runtimeDepsPath + Platform.pathSeparator + filename)
          ..createSync(recursive: true)
          ..writeAsBytesSync(data);
      }
    }

    // The `package.manifest` file comes from the tar extracted above.
    manifestPath =
        Platform.script.resolve('runtime_deps/package.manifest').toFilePath();
  });

  tearDownAll(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();

    printErrorHelp();
  });
  group('Package Manager', () {
    String originalRewriteRuleJson;
    Set<String> originalRepos;
    PackageManagerRepo repoServer;
    String testPackageName = 'cts-package-manager-sample-component';
    String testRepoRewriteRule =
        '{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"%%NAME%%","path_prefix_match":"/","path_prefix_replacement":"/"}]}';

    setUp(() async {
      repoServer = await PackageManagerRepo.initRepo(sl4fDriver, pmPath, log);

      // Gather the original package management settings before test begins.
      originalRepos = await getCurrentRepos(sl4fDriver);
      originalRewriteRuleJson = (await repoServer.pkgctlRuleDumpdynamic(
              'Save original rewrite rules from `pkgctl rule dump-dynamic`', 0))
          .stdout
          .toString();
    });
    tearDown(() async {
      if (!await resetPkgctl(
          sl4fDriver, originalRepos, originalRewriteRuleJson)) {
        log.severe('Failed to reset pkgctl to default state');
      }
      if (repoServer != null) {
        repoServer
          ..kill()
          ..cleanup();
      }
    });
    test(
        'Test that creates a repository, registers it using pkgctl, and validates that the '
        'package in the repository is visible.', () async {
      // Covers these commands (success cases only):
      //
      // Newly covered:
      // pkgctl get-hash fuchsia-pkg://<repo URL>/<package name>
      // pkgctl rule dump-dynamic
      // pkgctl repo add url <repo URL> -f 1
      // pkgctl repo rm fuchsia-pkg://<repo URL>
      // pkgctl repo
      // pm serve -repo=<path> -l :<port>
      await repoServer.setupServe('$testPackageName-0.far', manifestPath, []);
      final optionalPort = repoServer.getServePort();
      expect(optionalPort.isPresent, isTrue);
      final port = optionalPort.value;

      String repoName = 'pm-test-repo';
      String repoUrl = 'fuchsia-pkg://$repoName';

      // Confirm our serve is serving what we expect.
      log.info('Getting the available packages');
      final curlResponse = await Process.run(
          'curl', ['http://localhost:$port/targets.json', '-i']);

      log.info('curl response: ${curlResponse.stdout.toString()}');
      expect(curlResponse.exitCode, 0);
      final curlOutput = curlResponse.stdout.toString();
      expect(curlOutput.contains('$testPackageName/0'), isTrue);

      var gethashOutput = (await repoServer.pkgctlGethash(
              'Should error when checking for the package',
              '$repoUrl/$testPackageName',
              1))
          .stdout
          .toString();

      // Record what the rule list is before we begin, and confirm that is
      // the rule list when we are finished.
      final originalRuleList = (await repoServer.pkgctlRuleDumpdynamic(
              'Recording the current rule list', 0))
          .stdout
          .toString();

      await repoServer.pkgctlRepoAddUrlNF(
          'Adding the new repository ${repoServer.getRepoPath()} as an update source with http://$hostAddress:$port/config.json',
          'http://$hostAddress:$port/config.json',
          repoName,
          '1',
          0);

      // Check that our new repo source is listed.
      var listSrcsOutput = (await repoServer.pkgctlRepo(
              'Running pkgctl repo to list sources', 0))
          .stdout
          .toString();

      log.info('listSrcsOutput: $listSrcsOutput');
      expect(listSrcsOutput.contains(repoUrl), isTrue);

      gethashOutput = (await repoServer.pkgctlGethash(
              'Checking if the package now exists',
              '$repoUrl/$testPackageName',
              0))
          .stdout
          .toString();

      log.info('gethashOutput: $gethashOutput');

      expect(
          gethashOutput
              .contains('Error: Failed to get package hash with error:'),
          isFalse);

      var ruleListOutput = (await repoServer.pkgctlRuleDumpdynamic(
              'Confirm rule list did not change.', 0))
          .stdout
          .toString();
      expect(ruleListOutput, originalRuleList);

      await repoServer.pkgctlRepoRm('Delete $repoUrl', repoUrl, 0);

      // Check that our new repo source is gone.
      listSrcsOutput = (await repoServer.pkgctlRepo(
              'Running pkgctl repo to list sources', 0))
          .stdout
          .toString();

      log.info('listSrcsOutput: $listSrcsOutput');
      expect(listSrcsOutput.contains(repoUrl), isFalse);

      ruleListOutput = (await repoServer.pkgctlRuleDumpdynamic(
              'Confirm rule list did not change.', 0))
          .stdout
          .toString();
      expect(ruleListOutput, originalRuleList);

      log.info(
          'Killing serve process and ensuring the output contains `[pm serve]`.');
      final killStatus = repoServer.kill();
      expect(killStatus, isTrue);

      var serveOutputBuilder = StringBuffer();
      var serveProcess = repoServer.getServeProcess();
      expect(serveProcess.isPresent, isTrue);
      await repoServer
          .getServeStdoutSplitStream()
          .transform(utf8.decoder)
          .listen((data) {
        serveOutputBuilder.write(data);
      }).asFuture();
      final serveOutput = serveOutputBuilder.toString();
      // Ensuring that `[pm serve]` appears in the output because the `-q` flag
      // wasn't used in the serve command.
      expect(serveOutput.contains('[pm serve]'), isTrue);
    });
    test(
        'Test that creates a repository, deploys a package, and '
        'validates that the deployed package is visible from the server. '
        'Then make sure `pm serve` output is quiet.', () async {
      // Covers these commands (success cases only):
      //
      // Newly covered:
      // pm serve -repo=<path> -l :<port> -q
      //
      // Previously covered:
      // pkgctl repo add url http://<host>:<port>/config.json -n testhost -f 1
      await repoServer
          .setupServe('$testPackageName-0.far', manifestPath, ['-q']);
      final optionalPort = repoServer.getServePort();
      expect(optionalPort.isPresent, isTrue);
      final port = optionalPort.value;

      log.info('Getting the available packages');
      final curlResponse = await Process.run(
          'curl', ['http://localhost:$port/targets.json', '-i']);

      log.info('curl response: ${curlResponse.stdout.toString()}');
      expect(curlResponse.exitCode, 0);
      final curlOutput = curlResponse.stdout.toString();
      expect(curlOutput.contains('$testPackageName/0'), isTrue);

      await repoServer.pkgctlRepoAddUrlNF(
          'Adding the new repository as an update source with http://$hostAddress:$port',
          'http://$hostAddress:$port/config.json',
          'testhost',
          '1',
          0);

      log.info(
          'Killing serve process and ensuring the output does not contain `[pm serve]`.');
      final killStatus = repoServer.kill();
      expect(killStatus, isTrue);

      var serveOutputBuilder = StringBuffer();
      var serveProcess = repoServer.getServeProcess();
      expect(serveProcess.isPresent, isTrue);
      await repoServer
          .getServeStdoutSplitStream()
          .transform(utf8.decoder)
          .listen((data) {
        serveOutputBuilder.write(data);
      }).asFuture();
      final serveOutput = serveOutputBuilder.toString();
      // The `-q` flag was given to `pm serve`, so there should be no serve output.
      expect(serveOutput.contains('[pm serve]'), isFalse);
    });
    test('Test pkgctl default name behavior when no name is given.', () async {
      // Covers these commands (success cases only):
      //
      // Newly covered:
      // pkgctl repo add url http://<host>:<port>/config.json -f 1
      //
      // Previously covered:
      // pm serve -repo=<path> -l :<port>
      // pkgctl repo
      await repoServer.setupServe('$testPackageName-0.far', manifestPath, []);
      final optionalPort = repoServer.getServePort();
      expect(optionalPort.isPresent, isTrue);
      final port = optionalPort.value;

      await repoServer.pkgctlRepoAddUrlF(
          'Adding the new repository as an update source with http://$hostAddress:$port',
          'http://$hostAddress:$port/config.json',
          '1',
          0);

      var listSrcsOutput = (await repoServer.pkgctlRepo(
              'Running pkgctl repo to list sources', 0))
          .stdout
          .toString();

      log.info('Running pkgctl repo to list sources');
      String repoName = 'http://$hostAddress:$port';
      // Ensure the repo name complies to
      // https://fuchsia.dev/fuchsia-src/concepts/packages/package_url?hl=en#repository
      repoName = validRepoName(repoName);

      log.info('Checking repo name is $repoName');
      expect(listSrcsOutput.contains(repoName), isTrue);
    });
    test('Test `pm serve` writes its port number to a given file path.',
        () async {
      // Covers these commands (success cases only):
      //
      // Newly covered:
      // pm serve -repo=<path> -l :<port> -f <path to export port number>
      await repoServer.setupRepo('$testPackageName-0.far', manifestPath);
      final portFilePath = path.join(repoServer.getRepoPath(), 'port_file.txt');

      await repoServer.pmServeRepoLExtra(['-f', '$portFilePath']);
      final optionalPort = repoServer.getServePort();
      expect(optionalPort.isPresent, isTrue);
      final port = optionalPort.value;

      log.info('Checking that $portFilePath was generated with content: $port');
      final retryOptions = RetryOptions(maxAttempts: 5);
      String portString =
          await retryOptions.retry(File(portFilePath).readAsStringSync);
      expect(portString, isNotNull);
      expect(int.parse(portString), port);
    });
    test(
        'Test `pm serve` chooses its own port number and writes it to a given file path.',
        () async {
      // Covers these commands (success cases only):
      //
      // Newly covered:
      // pm serve -repo=<path> -l :0 -f <path to export port number>
      await repoServer.setupRepo('$testPackageName-0.far', manifestPath);

      await repoServer.pmServeRepoLFExtra([]);
      final optionalPort = repoServer.getServePort();
      expect(optionalPort.isPresent, isTrue);

      log.info('Checking port ${optionalPort.value} is valid.');
      final curlResponse = await Process.run('curl',
          ['http://localhost:${optionalPort.value}/targets.json', '-i']);

      log.info('curl response: ${curlResponse.stdout.toString()}');
      expect(curlResponse.exitCode, 0);
      final curlOutput = curlResponse.stdout.toString();
      expect(curlOutput.contains('$testPackageName/0'), isTrue);
    });
    test('Test `pkgctl resolve` base case.', () async {
      // Covers these commands (success cases only):
      //
      // Newly covered:
      // pkgctl resolve fuchsia-pkg://fuchsia.com/<name>
      var resolveProcessResult = await repoServer.pkgctlResolve(
          'Confirm that `$testPackageName` does not exist.',
          'fuchsia-pkg://fuchsia.com/$testPackageName',
          1);
      expect(resolveProcessResult.exitCode, isNonZero);
      expect(
          resolveProcessResult.stdout.toString(),
          equals(
              'resolving fuchsia-pkg://fuchsia.com/cts-package-manager-sample-component\n'));

      await repoServer.setupServe('$testPackageName-0.far', manifestPath, []);
      final optionalPort = repoServer.getServePort();
      expect(optionalPort.isPresent, isTrue);
      final port = optionalPort.value;

      // The repo path usually contains disallowed characters like '/' and
      // uppercase characters. pkgctl will return an error in this case.
      // Ensure we have a repoNameFixed that is known to comply with
      // https://fuchsia.dev/fuchsia-src/concepts/packages/package_url?hl=en#repository
      var repoNameFixed = validRepoName(repoServer.getRepoPath());

      await repoServer.pkgctlRepoAddUrlNF(
          'Adding the new repository with http://$hostAddress:$port',
          'http://$hostAddress:$port/config.json',
          repoNameFixed,
          '1',
          0);

      var localRewriteRule = testRepoRewriteRule;
      localRewriteRule =
          localRewriteRule.replaceAll('%%NAME%%', '$repoNameFixed');
      await repoServer.pkgctlRuleReplace(
          'Setting rewriting rule for new repository', localRewriteRule, 0);

      resolveProcessResult = await repoServer.pkgctlResolve(
          'Confirm that `$testPackageName` now exists.',
          'fuchsia-pkg://fuchsia.com/$testPackageName',
          0);
      expect(resolveProcessResult.exitCode, isZero);
      expect(
          resolveProcessResult.stdout.toString(),
          equals(
              'resolving fuchsia-pkg://fuchsia.com/cts-package-manager-sample-component\n'));

      await repoServer.pkgctlRuleReplace(
          'Restoring rewriting rule to original state',
          originalRewriteRuleJson,
          0);
    });
    test('Test `pkgctl resolve --verbose` base case.', () async {
      // Covers these commands (success cases only):
      //
      // Newly covered:
      // pkgctl resolve --verbose fuchsia-pkg://fuchsia.com/<name>
      var resolveVProcessResult = await repoServer.pkgctlResolveV(
          'Confirm that `$testPackageName` does not exist.',
          'fuchsia-pkg://fuchsia.com/$testPackageName',
          1);
      expect(resolveVProcessResult.exitCode, isNonZero);
      expect(resolveVProcessResult.stdout.toString(),
          isNot(contains('package contents:\n')));

      await repoServer.setupServe('$testPackageName-0.far', manifestPath, []);
      final optionalPort = repoServer.getServePort();
      expect(optionalPort.isPresent, isTrue);
      final port = optionalPort.value;

      // The repo path usually contains disallowed characters like '/' and
      // uppercase characters. pkgctl will return an error in this case.
      // Ensure we have a repoNameFixed that is known to comply with
      // https://fuchsia.dev/fuchsia-src/concepts/packages/package_url?hl=en#repository
      var repoNameFixed = validRepoName(repoServer.getRepoPath());

      await repoServer.pkgctlRepoAddUrlNF(
          'Adding the new repository with http://$hostAddress:$port',
          'http://$hostAddress:$port/config.json',
          repoNameFixed,
          '1',
          0);

      var localRewriteRule = testRepoRewriteRule;
      localRewriteRule =
          localRewriteRule.replaceAll('%%NAME%%', '$repoNameFixed');
      await repoServer.pkgctlRuleReplace(
          'Setting rewriting rule for new repository', localRewriteRule, 0);

      resolveVProcessResult = await repoServer.pkgctlResolveV(
          'Confirm that `$testPackageName` now exists.',
          'fuchsia-pkg://fuchsia.com/$testPackageName',
          0);
      expect(resolveVProcessResult.exitCode, isZero);
      expect(resolveVProcessResult.stdout.toString(),
          contains('package contents:\n'));

      await repoServer.pkgctlRuleReplace(
          'Restoring rewriting rule to original state',
          originalRewriteRuleJson,
          0);
    });
    test(
        'Test the flow from repo creation, to archive generation, '
        'to using pkgctl and running the component on the device.', () async {
      // Covers several key steps:
      // 0. Sanity check. The given component is not already available in the repo.
      // 1. The given component is archived into a valid `.far`.
      // 2. We are able to create our own repo.
      // 3. We are able to serve our repo to a given Fuchsia device.
      // 4. The device is able to pull the given component from our repo.
      // 5. The given component contains the expected content.
      var resolveExitCode = (await repoServer.pkgctlResolve(
              'Confirm that `$testPackageName` does not exist.',
              'fuchsia-pkg://fuchsia.com/$testPackageName',
              1))
          .exitCode;
      expect(resolveExitCode, isNonZero);

      await repoServer.setupServe('$testPackageName-0.far', manifestPath, []);
      final optionalPort = repoServer.getServePort();
      expect(optionalPort.isPresent, isTrue);
      final port = optionalPort.value;

      var repoNameFixed = validRepoName(repoServer.getRepoPath());

      await repoServer.pkgctlRepoAddUrlNF(
          'Adding the new repository with http://$hostAddress:$port',
          'http://$hostAddress:$port/config.json',
          repoNameFixed,
          '1',
          0);

      var localRewriteRule = testRepoRewriteRule;
      localRewriteRule =
          localRewriteRule.replaceAll('%%NAME%%', '$repoNameFixed');
      await repoServer.pkgctlRuleReplace(
          'Setting rewriting rule for new repository', localRewriteRule, 0);

      var response = await sl4fDriver.ssh.run(
          'run fuchsia-pkg://fuchsia.com/$testPackageName#meta/cts-package-manager-sample.cmx');
      expect(response.exitCode, 0);
      expect(response.stdout.toString(), 'Hello, World!\n');
      response = await sl4fDriver.ssh.run(
          'run fuchsia-pkg://fuchsia.com/$testPackageName#meta/cts-package-manager-sample2.cmx');
      expect(response.exitCode, 0);
      expect(response.stdout.toString(), 'Hello, World2!\n');

      await repoServer.pkgctlRuleReplace(
          'Restoring rewriting rule to original state',
          originalRewriteRuleJson,
          0);
    });
    test(
        'Test the flow from repo creation, to archive generation, '
        'to using pkgctl and running the component on the device.', () async {
      // Covers several key steps:
      // 1. The given component is archived into a valid `.far`.
      // 2. We are able to create our own repo.
      // 3. We are able to serve our repo to a given Fuchsia device.
      // 4. The device is able to pull the given component from our repo.
      // 5. The given component contains the expected content.
      await repoServer.setupServe('$testPackageName-0.far', manifestPath, []);
      final optionalPort = repoServer.getServePort();
      expect(optionalPort.isPresent, isTrue);
      final port = optionalPort.value;

      String repoName = 'pm-test-repo';
      String repoUrl = 'fuchsia-pkg://$repoName';

      await repoServer.pkgctlRepoAddUrlNF(
          'Adding the new repository as an update source with http://$hostAddress:$port',
          'http://$hostAddress:$port/config.json',
          repoName,
          '1',
          0);

      var localRewriteRule = testRepoRewriteRule;
      localRewriteRule = localRewriteRule.replaceAll('%%NAME%%', '$repoName');
      await repoServer.pkgctlRuleReplace(
          'Setting rewriting rule for new repository', localRewriteRule, 0);

      var response = await sl4fDriver.ssh.run(
          'run $repoUrl/$testPackageName#meta/cts-package-manager-sample.cmx');
      expect(response.exitCode, 0);
      expect(response.stdout.toString(), 'Hello, World!\n');
      response = await sl4fDriver.ssh.run(
          'run $repoUrl/$testPackageName#meta/cts-package-manager-sample2.cmx');
      expect(response.exitCode, 0);
      expect(response.stdout.toString(), 'Hello, World2!\n');

      await repoServer.pkgctlRuleReplace(
          'Restoring rewriting rule to original state',
          originalRewriteRuleJson,
          0);
    });
  }, timeout: _timeout);
}
