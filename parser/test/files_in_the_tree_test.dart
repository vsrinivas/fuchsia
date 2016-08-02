// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:parser/parser.dart';
import 'package:path/path.dart' as path;
import 'package:test/test.dart';

void main() {
  group('Check files in the tree', () {
    test('Parse manifests in /examples', () async {
      final rootDir = await (new Directory('../examples'));
      final files = rootDir.list(recursive: true, followLinks: false);
      await for (FileSystemEntity entity in files) {
        if (entity is File && path.basename(entity.path) == 'manifest.yaml') {
          if (entity.path.contains('/.')) {
            // Ignore hidden directories.
            continue;
          }
          expect(() async {
            await parseManifestFile(entity.path);
          }, returnsNormally,
              reason: 'Failed to parse manifest ${entity.path}');
        }
      }
    });

    test('Parse multiple-manifest files in /examples', () async {
      final rootDir = await (new Directory('../examples'));
      final files = rootDir.list(recursive: true, followLinks: false);
      await for (FileSystemEntity entity in files) {
        if (entity is File &&
            path.basename(entity.path).endsWith('manifests.yaml')) {
          final String content = await entity.readAsString();
          expect(() {
            parseManifests(content);
          }, returnsNormally,
              reason: 'Failed to parse multiple-manifest file ${entity.path}');
        }
      }
    });

    test('Parse recipes in /examples', () async {
      final rootDir = await (new Directory('../examples'));
      final files = rootDir.list(recursive: true, followLinks: false);
      await for (FileSystemEntity entity in files) {
        if (!(entity is File) || !path.basename(entity.path).endsWith('yaml')) {
          continue;
        }
        final String content = await (entity as File).readAsString();
        if (!content.contains('recipe:')) {
          continue;
        }
        expect(() async {
          await parseRecipeFile(entity.path);
        }, returnsNormally, reason: 'Failed to parse recipe ${entity.path}');
      }
    });
  });
}
