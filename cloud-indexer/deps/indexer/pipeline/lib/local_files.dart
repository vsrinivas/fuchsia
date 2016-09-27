// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';
import 'dart:async';

import 'package:modular_core/log.dart';
import 'package:path/path.dart' as path;

import 'index.dart';

final Logger _log = log("IndexerPipeline");

class LocalArtifacts {
  final List<File> manifestFiles = [];
  final List<File> recipeFiles = [];
}

/// Finds manifest and recipe files in the given directory (including
/// sub-directories).
Future<LocalArtifacts> _findLocalArtifacts(final Directory directory) async {
  final LocalArtifacts result = new LocalArtifacts();

  await for (FileSystemEntity entity
      in directory.list(recursive: true, followLinks: false)) {
    if (entity is! File) {
      continue;
    }

    // Ignore any entity in third_party.
    if (entity.path.contains('third_party')) {
      continue;
    }

    // Ignore hidden files and directories.
    if (entity.path.contains('/.')) {
      continue;
    }

    if (path.basename(entity.path) == 'manifest.yaml') {
      result.manifestFiles.add(entity);
    } else if (path.basename(entity.path).endsWith('.yaml')) {
      final String content = await (entity as File).readAsString();
      if (content.contains('recipe:')) {
        result.recipeFiles.add(entity);
      }
    }
  }

  return result;
}

/// Adds manifests and recipes in the given directories to the index.
Future<Null> indexLocalFiles(Index index, Iterable<Directory> directories,
    final Directory root, final String mojoHost) async {
  for (Directory directory in directories) {
    final LocalArtifacts artifacts = await _findLocalArtifacts(directory);
    for (final File manifestFile in artifacts.manifestFiles) {
      try {
        await index.addManifestFile(manifestFile);
      } catch (e) {
        _log.severe('Failed to parse manifest file: ${manifestFile.path} $e');
        rethrow;
      }
    }

    for (final File recipeFile in artifacts.recipeFiles) {
      final bool makeLink = mojoHost.isNotEmpty;
      final String url = makeLink
          ? path.join(mojoHost, path.relative(recipeFile.path, from: root.path))
          : null;
      final String name = path.withoutExtension(path.basename(recipeFile.path));
      index.addRecipe(name, url);
    }
  }
}
