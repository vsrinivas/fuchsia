// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:parser/cardinality.dart';
import 'package:parser/expression.dart';
import 'package:parser/parse_error.dart';
import 'package:parser/parser.dart';
import 'package:parser/recipe.dart';
import 'package:test/test.dart';

void main() {
  group('Recipe', () {
    const String title = 'ThisIsTheTitle';

    const String token1 = 'token1';
    const String token2 = 'token2';
    const String token3 = 'token3';

    final Uri verb1 = new Uri.http('verb.tq.io', '1');
    final Uri verb2 = new Uri.http('verb.tq.io', '2');
    final Uri verb3 = new Uri.http('verb.tq.io', '3');

    final Uri type1 = new Uri.http('type.tq.io', '1');
    final Uri type2 = new Uri.http('type.tq.io', '2');
    final Uri type3 = new Uri.http('type.tq.io', '3');
    final Uri type4 = new Uri.http('type.tq.io', '4');

    final Uri test1 = new Uri.http('test.tq.io', '1');

    final Label label1 = new Label.fromUri(type1);
    final Label label2 = new Label.fromUri(type2);
    final Label label3 = new Label.fromUri(type3);
    final Label label4 = new Label.fromUri(type4);

    final Property property1 = new Property([label1]);
    final Property property2 = new Property([label2]);
    final Property property3 = new Property([label3]);
    final Property property4 = new Property([label4]);

    final Property property1r = new Property([label1], Cardinality.repeated);

    final Property property3o = new Property([label3], Cardinality.optional);

    // Matcher for parser's ParseError.
    final throwsParseError = throwsA(new isInstanceOf<ParseError>());

    test('Parse title', () {
      final String yaml = '''
            title: $title
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(recipe.title, equals(title));
    });

    test('Parse verb uri', () {
      final String yaml = '''
            title: $title
            verb: $verb1
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(recipe.verb.label, equals(new Label.fromUri(verb1)));
    });

    test('Parse verb shorthand', () {
      final String yaml = '''
            title: $title
            verb: $token1
            use:
            - $token1: $verb1
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(recipe.verb.label, equals(new Label.fromUri(verb1)));
    });

    test('Parse use shorthand - no duplicates', () {
      final String yaml = '''
            title: $title
            use:
            - $token1: $type1
            - $token1: $type2
          ''';
      expect(() {
        parseRecipe(yaml);
      }, throwsParseError);
    });

    test('Input is an expression', () {
      final String yaml = '''
            title: $title
            verb: $verb1
            recipe:
            - input: $type1 {$type2}
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(
          recipe.steps[0].input,
          equals([
            new PathExpr([property1, property2])
          ]));
    });

    test('Input is an expression and uses shorthand', () {
      final String yaml = '''
            title: $title
            verb: $verb1
            recipe:
            - input: $token1 {$type2}
            use:
            - $token1: $type1
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(
          recipe.steps[0].input,
          equals([
            new PathExpr([property1, property2])
          ]));
    });

    test('Use can have multiple entries', () {
      final String yaml = '''
            title: $title
            verb: $verb1
            recipe:
            - input: $token1 {$type2, $token3, $type4}
            use:
            - $token1: $type1
            - $token3: $type3
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(
          recipe.steps[0].input,
          equals([
            new PathExpr([property1, property2]),
            new PathExpr([property1, property3]),
            new PathExpr([property1, property4])
          ]));
    });

    test('Flatten input expected properties', () {
      final String yaml = '''
          recipe:
          - input: $type1 {$type2, $type3}
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(
          recipe.steps[0].input,
          equals([
            new PathExpr([property1, property2]),
            new PathExpr([property1, property3])
          ]));
    });

    test('Output is an expression', () {
      final String yaml = '''
          recipe:
          - output: $type1 {$type2, $type3}
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(
          recipe.steps[0].output,
          equals([
            new PathExpr([property1, property2]),
            new PathExpr([property1, property3])
          ]));
    });

    test('Output is an expression and uses shorthand', () {
      final String yaml = '''
            title: $title
            verb: $verb1
            output: $token1
            use:
            - $token1: $type1
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(recipe.output, equals([new PathExpr.single(property1)]));
    });

    test('Output is specified multiple times', () {
      final String yaml = '''
            title: $title
            verb: $verb1
            output:
            - $type1
            - $type2
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(
          recipe.output,
          equals([
            new PathExpr.single(property1),
            new PathExpr.single(property2)
          ]));
    });

    test('Parse step verb uri', () {
      final String yaml = '''
          title: $title
          verb: $verb1
          recipe:
          - verb: $verb2
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(recipe.steps[0].verb.label, equals(new Label.fromUri(verb2)));
    });

    test('Parse step verb shorthand', () {
      final String yaml = '''
          title: $title
          verb: $verb1
          recipe:
          - verb: $token1
          use:
          - $token1: $verb2
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(recipe.steps[0].verb.label, equals(new Label.fromUri(verb2)));
    });

    test('Step outputs are expressions, too', () {
      final String yaml = '''
          title: $title
          verb: $verb1
          recipe:
          - verb: $verb2
            output: $type1 -> $type2
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(
          recipe.steps[0].output,
          equals([
            new PathExpr([property1, property2])
          ]));
    });

    test('There can be multiple Step outputs', () {
      final String yaml = '''
          title: $title
          verb: $verb1
          recipe:
          - verb: $verb2
            output:
            - $type1
            - $type2
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(
          recipe.steps[0].output,
          equals([
            new PathExpr.single(property1),
            new PathExpr.single(property2)
          ]));
    });

    test('Step outputs use shorthands also!', () {
      final String yaml = '''
          title: $title
          verb: $verb1
          recipe:
          - verb: $verb2
            output:
            - $token1
            - $token2
          use:
          - $token1: $type1
          - $token2: $type2
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(
          recipe.steps[0].output,
          equals([
            new PathExpr.single(property1),
            new PathExpr.single(property2)
          ]));
    });

    test('Steps have input expressions', () {
      final String yaml = '''
          title: $title
          verb: $verb1
          recipe:
          - verb: $verb2
            input: $type1 -> $type2
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(
          recipe.steps[0].input,
          equals([
            new PathExpr([property1, property2])
          ]));
    });

    test('Step inputs use shorthand (what!)', () {
      final String yaml = '''
          title: $title
          verb: $verb1
          recipe:
          - verb: $verb2
            input: $token1
          use:
          - $token1: $type1
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(recipe.steps[0].input, equals([new PathExpr.single(property1)]));
    });

    test('Step inputs have multiple values', () {
      final String yaml = '''
          title: $title
          verb: $verb1
          recipe:
          - verb: $verb2
            input:
            - $type1
            - $type2
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(
          recipe.steps[0].input,
          equals([
            new PathExpr.single(property1),
            new PathExpr.single(property2)
          ]));
    });

    test('Parse step: put it all together', () {
      final String yaml = '''
          title: $title
          verb: $verb1
          recipe:
          - verb: $token1
            input: $type1+
            output:
            - $token2
          - verb: $verb3
            input:
            - $token3?
            output: $type4
          use:
          - $token1: $verb2
          - $token2: $type2
          - $token3: $type3
          ''';
      final Recipe recipe = parseRecipe(yaml);
      final Verb step1Verb = new Verb(new Label.fromUri(verb2));
      final PathExpr step1Input = new PathExpr.single(property1r);
      final PathExpr step1Output = new PathExpr.single(property2);
      final Step step1 =
          new Step(null, step1Verb, [step1Input], [step1Output], [], [], null);
      final Verb step2Verb = new Verb(new Label.fromUri(verb3));
      final PathExpr step2Input = new PathExpr.single(property3o);
      final PathExpr step2Output = new PathExpr.single(property4);
      final Step step2 =
          new Step(null, step2Verb, [step2Input], [step2Output], [], [], null);
      expect(recipe.steps, equals([step1, step2]));
    });

    test('Scope is an expression', () {
      final String yaml = '''
            title: $title
            verb: $verb1
            recipe:
            - verb: $verb2
              scope: $type1 -> $type2
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(
          recipe.steps[0].scope, equals(new PathExpr([property1, property2])));
    });

    test('Scope can use shorthands also like everything else', () {
      final String yaml = '''
            title: $title
            verb: $verb1
            recipe:
            - verb: $verb2
              scope: $token1
            use:
            - $token1: $type1
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(recipe.steps[0].scope, equals(new PathExpr.single(property1)));
    });

    test('Parse absolute test reference', () {
      final String yaml = '''
        title: $title
        verb: $verb1
        test: $test1
        ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(recipe.test, equals(test1));
    });

    test('Parse failure for relative test reference', () {
      final String yaml = '''
        title: $title
        verb: $verb1
        test: /relative/test
        ''';
      expect(() {
        parseRecipe(yaml);
      }, throwsParseError);
    });

    test('Parse failure for invalid test: map', () {
      final String yaml = '''
        title: $title
        verb: $verb1
        test:
          foo: bar
        ''';
      expect(() {
        parseRecipe(yaml);
      }, throwsParseError);
    });

    test('Parse failure for invalid test: sequence', () {
      final String yaml = '''
        title: $title
        verb: $verb1
        test:
          - foo: bar
          - baz: foo
        ''';
      expect(() {
        parseRecipe(yaml);
      }, throwsParseError);
    });

    test('Parse url in step', () {
      final String yaml = '''
            title: $title
            verb: $verb1
            recipe:
            - verb: $verb1
              input: $token1
              output: $token2
            - verb: $verb2
              input: $token1
              output: $token2
              url: http://example.com/module
            use:
            - $token1: $type1
            - $token2: $type2
          ''';
      final Recipe recipe = parseRecipe(yaml);
      expect(recipe.steps[0].url, isNull);
      expect(
          recipe.steps[1].url, equals(new Uri.http('example.com', 'module')));
    });

    test('Serialization', () {
      final String yaml = '''
            title: $title
            verb: $verb1
            recipe:
            - verb: $verb1
              input: $token1
              output: $token2
            - verb: $verb2
              input: $token1
              output: $token2
              url: http://example.com/module
            use:
            - $token1: $type1
            - $token2: $type2
          ''';
      final Recipe recipe = parseRecipe(yaml);
      final Recipe otherRecipe =
          new Recipe.fromJsonString(recipe.toJsonString());
      expect(recipe, equals(otherRecipe));
    });
  });
}
