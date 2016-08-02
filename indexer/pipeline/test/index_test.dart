// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:indexer_pipeline/index.dart';
import 'package:parser/parse_error.dart';
import 'package:parser/manifest.dart';
import 'package:test/test.dart';

void main() {
  group('index', () {
    test('manifests', () {
      final Index index = new Index();
      expect(index.manifests.length, equals(0));

      // Try to add a manifest that doesn't parse correctly.
      expect(() {
        index.addManifest("verb: undeclared");
      }, throwsA(new isInstanceOf<ParseError>()));
      expect(index.manifests.length, equals(0));

      // Add two manifests that do parse correctly.
      index.addManifest('verb: https://tq.io/do-stuff');
      index.addManifest('verb: https://tq.io/do-things');

      expect(index.manifests.length, equals(2));
      expect(index.manifests[0],
          equals(new Manifest.parseYamlString('verb: https://tq.io/do-stuff')));
      expect(index.manifests[1],
          equals(new Manifest.parseYamlString('verb: https://tq.io/do-things')));
    });

    test('recipes', () {
      final Index index = new Index();
      expect(index.recipes.length, equals(0));

      index.addRecipe('abc', 'https://tq.io/abc');
      index.addRecipe('def', 'https://tq.io/def');

      expect(index.recipes.length, equals(2));
      expect(index.recipes[0].name, equals('abc'));
      expect(index.recipes[0].url, equals('https://tq.io/abc'));
      expect(index.recipes[1].name, equals('def'));
      expect(index.recipes[1].url, equals('https://tq.io/def'));
    });
  });
}
