// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:handler/manifest_matcher.dart';
import 'package:parser/manifest.dart';
import 'package:parser/parser.dart';
import 'package:parser/recipe.dart';
import 'package:test/test.dart';

void main() {
  group('Manifest matcher', () {
    test('Match manifest to step direct', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p2
            - p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        input:
         - p2
         - p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(manifest));
    });

    test('Match manifest to step indirect', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1 -> p2
            - p1 -> p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        input:
         - p2
         - p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(manifest));
    });

    test('Match manifest to step direct, no match', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p2
            - p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        input:
         - p1
         - p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(null));
    });

    test('Match manifest to step indirect repeated', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1+ -> p2
            - p1 -> p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        input:
         - p2+
         - p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(manifest));
    });

    test('Match manifest to step indirect repeated, no match', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1+ -> p2+
            - p1 -> p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        input:
         - p2
         - p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(null));
    });

    test('Match manifest to step indirect repeated, no match', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1+ -> p2
            - p1 -> p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        input:
         - p2
         - p3

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(null));
    });

    test('Recipe has 1 display label - manifest has none, no match', () {
      final String recipeText = '''
        title: foo
        recipe:
         - verb: v1
           display: bar

        use:
         - bar: http://tq.io/bar
         - v1: http://tq.io/v1
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1

        use:
         - v1: http://tq.io/v1
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(null));
    });

    test('Manifest and recipe display types are different, no match', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           display: bar

        use:
         - bar: http://tq.io/bar
         - v1: http://tq.io/v1
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        display: foo

        use:
         - v1: http://tq.io/v1
         - foo: http://tq.io/foo
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(null));
    });

    test('Manifest does not contain display type from recipe, no match', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           display: bak

        use:
         - bak: http://tq.io/bak
         - v1: http://tq.io/v1
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        display:
         - foo
         - bar
         - baz

        use:
         - bar: http://tq.io/bar
         - baz: http://tq.io/baz
         - foo: http://tq.io/foo
         - v1: http://tq.io/v1
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(null));
    });

    test('Manifest does not contain all display types from recipe, no match',
        () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           display:
            - bar
            - baz

        use:
         - bar: http://tq.io/bar
         - baz: http://tq.io/baz
         - v1: http://tq.io/v1
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        display:
         - foo
         - bar

        use:
         - bar: http://tq.io/bar
         - foo: http://tq.io/foo
         - v1: http://tq.io/v1
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(null));
    });

    test('Manifest and recipe display types are identical', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           display: bar

        use:
         - bar: http://tq.io/bar
         - v1: http://tq.io/v1
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        display: bar

        use:
         - bar: http://tq.io/bar
         - v1: http://tq.io/v1
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(manifest));
    });

    test('Recipe display type is subset of manifest display types', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           display: bar

        use:
         - bar: http://tq.io/bar
         - v1: http://tq.io/v1
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        display:
         - foo
         - bar

        use:
         - bar: http://tq.io/bar
         - foo: http://tq.io/foo
         - v1: http://tq.io/v1
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(manifest));
    });

    test('Recipe display types form subset of manifest display types', () {
      final String recipeText = '''
        title: foo
        recipe:
         - verb: v1
           display:
            - foo
            - baz

        use:
         - baz: http://tq.io/baz
         - foo: http://tq.io/foo
         - v1: http://tq.io/v1
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        display:
         - foo
         - bar
         - baz

        use:
         - bar: http://tq.io/bar
         - baz: http://tq.io/baz
         - foo: http://tq.io/foo
         - v1: http://tq.io/v1
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(manifest));
    });

    test('Recipe has 1 compose label - manifest has none, no match', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           compose: bar

        use:
         - bar: http://tq.io/bar
         - v1: http://tq.io/v1
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1

        use:
         - v1: http://tq.io/v1
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(null));
    });

    test('Manifest and recipe compose types are different, no match', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           compose: bar

        use:
         - bar: http://tq.io/bar
         - v1: http://tq.io/v1
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        compose: foo

        use:
         - foo: http://tq.io/foo
         - v1: http://tq.io/v1
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(null));
    });

    test('Manifest does not contain compose type from recipe, no match', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           compose: bak

        use:
         - v1: http://tq.io/v1
         - bak: http://tq.io/bak
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        compose:
         - foo
         - bar
         - baz

        use:
         - bar: http://tq.io/bar
         - baz: http://tq.io/baz
         - foo: http://tq.io/foo
         - v1: http://tq.io/v1
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(null));
    });

    test('Manifest does not contain all compose types from recipe, no match',
        () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           compose:
            - bar
            - baz

        use:
         - bar: http://tq.io/bar
         - baz: http://tq.io/baz
         - v1: http://tq.io/v1
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        compose:
         - foo
         - bar

        use:
         - bar: http://tq.io/bar
         - foo: http://tq.io/foo
         - v1: http://tq.io/v1
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(null));
    });

    test('Manifest and recipe compose types are identical', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           compose: bar

        use:
         - bar: http://tq.io/bar
         - v1: http://tq.io/v1
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        compose: bar

        use:
         - bar: http://tq.io/bar
         - v1: http://tq.io/v1
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(manifest));
    });

    test('Recipe compose type is subset of manifest compose types', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           compose: bar

        use:
         - bar: http://tq.io/bar
         - v1: http://tq.io/v1
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        compose:
         - foo
         - bar

        use:
         - bar: http://tq.io/bar
         - foo: http://tq.io/foo
         - v1: http://tq.io/v1
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(manifest));
    });

    test('Recipe compose types form subset of manifest compose types', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           compose:
            - foo
            - baz

        use:
         - baz: http://tq.io/baz
         - foo: http://tq.io/foo
         - v1: http://tq.io/v1
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        compose:
         - foo
         - bar
         - baz

        use:
         - bar: http://tq.io/bar
         - baz: http://tq.io/baz
         - foo: http://tq.io/foo
         - v1: http://tq.io/v1
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(manifest));
    });

    test('Match manifest input order changed', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input:
            - p1
            - p2
            - p3
           output:
            - p3
            - p2
            - p1

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        input:
         - p2
         - p3
         - p1
        output:
         - p1
         - p3
         - p2

        use:
         - v1: http://tq.io/v1
         - p1: http://tq.io/p1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(manifest));
    });

    test('Match manifest ignore output cardinality', () {
      final String recipeText = '''
        title: foo
        recipe:
         - verb: v1
           output:
            - p2
            - p3

        use:
         - v1: http://tq.io/v1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText = '''
        verb: v1
        output:
         - p2
         - p3

        use:
         - v1: http://tq.io/v1
         - p2: http://tq.io/p2
         - p3: http://tq.io/p3
      ''';
      final Manifest manifest = parseManifest(manifestText);
      final ManifestMatcher matcher = new ManifestMatcher([manifest]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(manifest));
    });

    test('Recipe step with url', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           url: http://example.com/module

        use:
         - v1: http://tq.io/v1
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText1 = '''
        verb: v1

        use:
         - v1: http://tq.io/v1
      ''';
      final String manifestText2 = '''
        verb: v1
        url: http://example.com/module

        use:
         - v1: http://tq.io/v1
      ''';

      final Manifest manifest1 = parseManifest(manifestText1);
      final Manifest manifest2 = parseManifest(manifestText2);
      final ManifestMatcher matcher =
          new ManifestMatcher([manifest1, manifest2]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(manifest2));
    });

    test('Recipe step with url but no match in manifests', () {
      final String recipeText = '''
        title: foo

        recipe:
         - verb: v1
           input: p1
           url: http://example.com/module

        use:
         - p1: http://tq.io/p1
         - v1: http://tq.io/v1
      ''';
      final Recipe recipe = parseRecipe(recipeText);

      final String manifestText1 = '''
        verb: v1
        input: p1

        use:
         - p1: http://tq.io/p1
         - v1: http://tq.io/v1
      ''';
      final String manifestText2 = '''
        verb: v1
        url: http://example.com/module

        use:
         - p1: http://tq.io/p1
         - v1: http://tq.io/v1
      ''';

      final Manifest manifest1 = parseManifest(manifestText1);
      final Manifest manifest2 = parseManifest(manifestText2);
      final ManifestMatcher matcher =
          new ManifestMatcher([manifest1, manifest2]);

      expect(matcher.selectManifest(recipe.steps[0]), equals(null));
    });
  });
}
