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
      const String testManifest1 = 'url: https://tq.io/do-stuff.mojo\n'
          'verb: https://tq.io/do-stuff';
      const String testManifest2 = 'url: https://tq.io/do-things.flx\n'
          'verb: https://tq.io/do-things';

      final Index index = new Index();
      expect(index.manifests.length, equals(0));

      // Try to add a manifest that doesn't parse correctly.
      expect(() {
        index.addManifest("verb: undeclared");
      }, throwsA(new isInstanceOf<ParseError>()));
      expect(index.manifests.length, equals(0));

      // Add two manifests that do parse correctly.
      index.addManifest(testManifest1);
      index.addManifest(testManifest2);

      expect(index.manifests.length, equals(2));
      expect(index.manifests[Uri.parse('https://tq.io/do-stuff.mojo')],
          equals(new Manifest.parseYamlString(testManifest1)));
      expect(index.manifests[Uri.parse('https://tq.io/do-things.flx')],
          equals(new Manifest.parseYamlString(testManifest2)));
    });

    test('recipes', () {
      final String name1 = 'abc';
      final String url1 = 'https://tq.io/abc';
      final String name2 = 'def';
      final String url2 = 'https://tq.io/def';

      final Index index = new Index();
      expect(index.recipes.length, equals(0));

      index.addRecipe(name1, url1);
      index.addRecipe(name2, url2);

      expect(index.recipes.length, equals(2));
      expect(index.recipes[Uri.parse(url1)].name, equals('abc'));
      expect(index.recipes[Uri.parse(url1)].url, equals('https://tq.io/abc'));
      expect(index.recipes[Uri.parse(url2)].name, equals('def'));
      expect(index.recipes[Uri.parse(url2)].url, equals('https://tq.io/def'));
    });

    test('remove-manifest', () {
      const String testManifest1 = 'url: https://tq.io/do-stuff.mojo\n'
          'verb: find\n'
          'use:\n'
          '- find: https://find.io';
      const String testManifest2 = 'url: https://tq.io/do-things.flx\n'
          'verb: seek\n'
          'use:\n'
          '- seek: https://find.io';

      final Index index = new Index();
      expect(index.manifests.length, equals(0));

      index.addManifest(testManifest1);
      index.addManifest(testManifest2);

      expect(index.manifests.length, equals(2));
      expect(index.manifests[Uri.parse('https://tq.io/do-stuff.mojo')],
          equals(new Manifest.parseYamlString(testManifest1)));
      expect(index.manifests[Uri.parse('https://tq.io/do-things.flx')],
          equals(new Manifest.parseYamlString(testManifest2)));

      Map<String, dynamic> verbRankings = index.verbRanking[0];
      expect(verbRankings['uri'], equals('https://find.io'));
      expect(verbRankings['referenceCount'], equals(2));
      expect(verbRankings['shorthands'], unorderedEquals(['find', 'seek']));

      index.removeManifest('https://tq.io/do-stuff.mojo');
      expect(index.manifests.length, equals(1));

      verbRankings = index.verbRanking[0];
      expect(verbRankings['uri'], equals('https://find.io'));
      expect(verbRankings['referenceCount'], equals(1));

      // Since we use a best-effort removal, 'find' is still in the shorthands.
      expect(verbRankings['shorthands'], unorderedEquals(['find', 'seek']));
    });

    test('replace-manifest', () {
      const String testManifest1 = 'url: https://tq.io/do-stuff.mojo\n'
          'verb: find\n'
          'use:\n'
          '- find: https://find.io';
      const String testManifest2 = 'url: https://tq.io/do-stuff.mojo\n'
          'verb: seek\n'
          'use:\n'
          '- seek: https://find.io';

      final Index index = new Index();
      expect(index.manifests.length, equals(0));

      index.addManifest(testManifest1);
      index.addManifest(testManifest2);

      expect(index.manifests.length, equals(1));
      expect(index.manifests[Uri.parse('https://tq.io/do-stuff.mojo')],
          equals(new Manifest.parseYamlString(testManifest1)));

      Map<String, dynamic> verbRankings = index.verbRanking[0];
      expect(verbRankings['uri'], equals('https://find.io'));
      expect(verbRankings['referenceCount'], equals(1));
      expect(verbRankings['shorthands'], unorderedEquals(['seek']));
    });
  });
}
