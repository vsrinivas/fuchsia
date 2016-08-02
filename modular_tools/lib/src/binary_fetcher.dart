// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:path/path.dart' as path;

import 'base/file_system.dart';
import 'base/process.dart';
import 'configuration.dart';

const List<String> _kRequiredModularBinaries = const <String>[
  'handler.mojo',
  // TODO(alhaad): The launcher should not be special cased (see
  // domokit/modular#772).
  'launcher.flx',
  'ledger.mojo',
  'suggestinator.mojo',
];

// |BinaryFetcher| is a utility to download and cache Modular binaries required
// to run outside the modular repository. It wraps over 'gsutil' available
// alond with depot_tools to download since the Modular CDN is private and
// requires authentication.
class BinaryFetcher {
  final EnvironmentConfiguration _environment;
  final TargetPlatform _target;

  BinaryFetcher(this._environment, this._target);

  // Copies all the binaries listed in |binaryNames| from |localSourceDirectory|
  // if specified, else from the Modular CDN to the build directory.
  // Throws an exception if any of the requested |binaryNames| are not found.
  Future<int> fetchBinaries({final String localSourceDirectory}) async {
    // Copy from local source.
    if (localSourceDirectory != null) {
      if (!await _checkIfBinariesExist(localSourceDirectory)) {
        print('Could not find Modular artifacts in $localSourceDirectory.');
        return 1;
      }
      print('Copying Modular binary artifacts..');
      await Future
          .wait(_kRequiredModularBinaries.map((final String binary) async {
        final String filePath = path.join(localSourceDirectory, binary);
        final File file = new File(filePath);
        await file.copy(path.join(_environment.buildDir, binary));
      }));
      return 0;
    }

    final String modularRevision = (await _getModularRevision()).trim();
    final Directory cacheDir = _getCacheDir(modularRevision);
    await ensureDirectoryExists(cacheDir.path, isDirPath: true);

    // Binaries were already available in the cache.
    if (await _checkIfBinariesExist(cacheDir.path)) {
      return fetchBinaries(localSourceDirectory: cacheDir.path);
    }

    print('Downloading Modular binaries from the cloud, one moment please..');
    if (await _downloadModularBinaries(cacheDir, modularRevision) != 0) {
      print("Failed downloading Modular binaries");
      return 1;
    }

    // Here, we should have the Modular binaries in the cache directory.
    return fetchBinaries(localSourceDirectory: cacheDir.path);
  }

  Future<int> _downloadModularBinaries(
      final Directory cacheDir, final String modularRevision) async {
    final String gsutilPath = await _getGsutilPath();
    final List<int> ret = await Future
        .wait(_kRequiredModularBinaries.map((final String binary) async {
      final String destinationPath = path.join(cacheDir.path, binary);
      final String sourcePath = 'gs://' +
          path.join('modular', 'services', targetPlatformToArchString[_target],
              modularRevision, binary);
      return await processNonBlocking(
          gsutilPath, ['cp', sourcePath, destinationPath]);
    }));
    return ret.fold(0, (prev, element) => prev | element);
  }

  Future<String> _getGsutilPath() async {
    final List<String> envPath = Platform.environment['PATH'].split(':');
    final List<String> possibleGclientPaths =
        await Future.wait(envPath.map((final String pathEntry) async {
      final String gclientPath = path.join(pathEntry, 'gclient.py');
      if (!await (new File(gclientPath).exists())) {
        return null;
      }
      return path.join(pathEntry, 'third_party', 'gsutil', 'gsutil');
    }));
    for (final String possibleGclientPath in possibleGclientPaths) {
      if (possibleGclientPath != null) {
        return possibleGclientPath;
      }
    }
    throw 'Failed to find depot_tools on PATH. Instructions can be found at '
        'https://www.chromium.org/developers/how-tos/install-depot-tools';
  }

  Future<bool> _checkIfBinariesExist(final String dir) async {
    bool binariesExist = true;
    await Future
        .wait(_kRequiredModularBinaries.map((final String binary) async {
      final String filePath = path.join(dir, binary);
      final File file = new File(filePath);
      if (!await file.exists()) {
        binariesExist = false;
      }
    }));
    return binariesExist;
  }

  Future<String> _getModularRevision() async {
    return (await processNonBlockingWithResult('git', ['rev-parse', 'HEAD'],
            workingDirectory: _environment.modularRoot))
        .stdout;
  }

  // Returns the path where the Modular binaries are cached.
  Directory _getCacheDir(final String modularRevision) =>
      new Directory(path.join(_environment.modularRoot, 'public', 'dart', 'lib',
          'cache', modularRevision, targetPlatformToArchString[_target]));
}
