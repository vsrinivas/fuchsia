// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

const _timeout = Timeout(Duration(minutes: 1));

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.RepositoryManager repo;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();

    repo = sl4f.RepositoryManager(sl4fDriver);
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  group(sl4f.RepositoryManager, () {
    test('repository manager facade', () async {
      // If anything throws an exception then we've failed.
      final repositories = await repo.list();
      expect(repositories, isNotNull);
      expect(repositories, isNotEmpty);
      expect(repositories.length, isNonZero);
      expect(repositories[0]['mirrors'], isNotEmpty);
      expect(repositories[0]['repo_url'], isNotEmpty);
      expect(repositories[0]['root_keys'], isNotEmpty);

      for (final repository in repositories) {
        final repoConfig = sl4f.RepositoryConfig.fromJson(repository);
        await repo.add(repoConfig);
      }
    });
  }, timeout: _timeout);
}
