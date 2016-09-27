// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:parser/cardinality.dart';
import 'package:parser/expression.dart';
import 'package:parser/expression_parser.dart';
import 'package:parser/parse_error.dart';
import 'package:test/test.dart';

void main() {
  final Label label1 =
      new Label(new Uri.http('test.tq.io', 'label1'), 'label1');
  final Label label2 =
      new Label(new Uri.http('test.tq.io', 'label2'), 'label2');
  final Label label3 =
      new Label(new Uri.http('test.tq.io', 'label3'), 'label3');
  final Label label4 =
      new Label(new Uri.http('test.tq.io', 'label4'), 'label4');
  final Label label5 =
      new Label(new Uri.http('test.tq.io', 'label5'), 'label5');

  final ParserState parserState = new ParserState();
  parserState.shorthand['label1'] = label1;
  parserState.shorthand['label2'] = label2;
  parserState.shorthand['label3'] = label3;
  parserState.shorthand['label4'] = label4;
  parserState.shorthand['label5'] = label5;

  group('label', () {
    test('real url', () {
      final String input = 'https://test.tq.io/real-url';
      final Scanner scanner =
          new Scanner(parserState, new SourceLocation.inline(), input);
      final Label parsed = parseLabel(scanner);
      expect(parsed.uri, equals(Uri.parse('https://test.tq.io/real-url')));
    });

    test('fail on undeclared shorthand', () {
      final String input = 'not-a-valid-url';
      final Scanner scanner =
          new Scanner(parserState, new SourceLocation.inline(), input);
      expect(() {
        parseLabel(scanner);
      }, throwsA(new isInstanceOf<ParseError>()));
    });
  });

  group('property', () {
    test('single url label', () {
      final String input = 'http://test.tq.io/label1';
      final Scanner scanner =
          new Scanner(parserState, new SourceLocation.inline(), input);
      final Property parsed = parseProperty(scanner);
      expect(parsed, equals(new Property([label1])));
    });

    test('multiple url labels', () {
      final String input =
          '(http://test.tq.io/label1 http://test.tq.io/label2)';
      final Scanner scanner =
          new Scanner(parserState, new SourceLocation.inline(), input);
      final Property parsed = parseProperty(scanner);
      expect(parsed, equals(new Property([label1, label2])));
    });

    test('multiple url labels with whitespace', () {
      final String input =
          '  (   http://test.tq.io/label1   http://test.tq.io/label2 )  ';
      final Scanner scanner =
          new Scanner(parserState, new SourceLocation.inline(), input);
      final Property parsed = parseProperty(scanner);
      expect(parsed, equals(new Property([label1, label2])));
    });

    test('error on multiple url labels without parens', () {
      final String input = 'http://test.tq.io/label1 http://test.tq.io/label2';
      final Scanner scanner =
          new Scanner(parserState, new SourceLocation.inline(), input);
      expect(() {
        parseProperty(scanner);
      }, throwsA(new isInstanceOf<ParseError>()));
    });
  });

  group('path single component', () {
    test('single-label singular', () {
      final String input = 'http://test.tq.io/label1';
      final Scanner scanner =
          new Scanner(parserState, new SourceLocation.inline(), input);
      final PatternExpr parsed = parsePattern(scanner);
      expect(
          parsed,
          equals(
              new PatternExpr(new Property([label1], Cardinality.singular))));
      expect(parsed.toString(), equals(input));
    });

    test('single-label repeated property', () {
      final String input = 'http://test.tq.io/label1+';
      final Scanner scanner =
          new Scanner(parserState, new SourceLocation.inline(), input);
      final PatternExpr parsed = parsePattern(scanner);
      expect(
          parsed,
          equals(
              new PatternExpr(new Property([label1], Cardinality.repeated))));
      expect(parsed.toString(), equals(input));
    });

    test('multi-label repeated property', () {
      final String input =
          '(http://test.tq.io/label1 http://test.tq.io/label2)+';
      final Scanner scanner =
          new Scanner(parserState, new SourceLocation.inline(), input);
      final PatternExpr parsed = parsePattern(scanner);
      expect(
          parsed,
          equals(new PatternExpr(
              new Property([label1, label2], Cardinality.repeated))));
      expect(parsed.toString(), equals(input));
    });
  });

  group('path linear', () {
    test('single-label', () {
      final String input =
          'http://test.tq.io/label1 -> http://test.tq.io/label2';
      final Scanner scanner =
          new Scanner(parserState, new SourceLocation.inline(), input);
      final PatternExpr parsed = parsePattern(scanner);
      expect(
          parsed,
          equals(new PatternExpr(new Property([label1], Cardinality.singular), [
            new PatternExpr(new Property([label2], Cardinality.singular))
          ])));
      expect(parsed.toString(), equals(input));
    });

    test('multiple-label', () {
      final String input = '(label1 label3) -> label2';
      final Scanner scanner =
          new Scanner(parserState, new SourceLocation.inline(), input);
      final PatternExpr parsed = parsePattern(scanner);
      expect(
          parsed,
          equals(new PatternExpr(
              new Property([label1, label3], Cardinality.singular), [
            new PatternExpr(new Property([label2], Cardinality.singular))
          ])));
      expect(parsed.toString(), equals(input));
    });

    test('cardinality singular', () {
      final String input = '(label1 label3)! -> label2!';
      final Scanner scanner =
          new Scanner(parserState, new SourceLocation.inline(), input);
      final PatternExpr parsed = parsePattern(scanner);
      expect(
          parsed,
          equals(new PatternExpr(
              new Property([label1, label3], Cardinality.singular), [
            new PatternExpr(new Property([label2], Cardinality.singular))
          ])));

      // TODO(mesch): Qualifier ! is not yet formatted.
      // expect(parsed.toString(), equals(input));
    });
  });

  group('path branched', () {
    test('single-label', () {
      final String input = 'label1 { label2, label3 }';
      final Scanner scanner =
          new Scanner(parserState, new SourceLocation.inline(), input);
      final PatternExpr parsed = parsePattern(scanner);
      expect(
          parsed,
          equals(new PatternExpr(new Property([label1], Cardinality.singular), [
            new PatternExpr(new Property([label2], Cardinality.singular)),
            new PatternExpr(new Property([label3], Cardinality.singular))
          ])));
      expect(parsed.toString(), equals(input));
    });

    test('multiple-label', () {
      final String input = 'label1 { label2, (label3 label4) }';
      final Scanner scanner =
          new Scanner(parserState, new SourceLocation.inline(), input);
      final PatternExpr parsed = parsePattern(scanner);
      expect(
          parsed,
          equals(new PatternExpr(new Property([label1], Cardinality.singular), [
            new PatternExpr(new Property([label2], Cardinality.singular)),
            new PatternExpr(
                new Property([label3, label4], Cardinality.singular))
          ])));
      expect(parsed.toString(), equals(input));
    });

    test('wildcard', () {
      final String input = '_ { label2, label3 }';
      final Scanner scanner =
          new Scanner(parserState, new SourceLocation.inline(), input);
      final PatternExpr parsed = parsePattern(scanner);
      expect(
          parsed,
          equals(new PatternExpr(new Property([], Cardinality.singular), [
            new PatternExpr(new Property([label2], Cardinality.singular)),
            new PatternExpr(new Property([label3], Cardinality.singular))
          ])));
      expect(parsed.toString(), equals(input));
    });

    test('with representation', () {
      final String input = 'label1 <label4> { label2 <label5>, label3 }';
      final Scanner scanner =
          new Scanner(parserState, new SourceLocation.inline(), input);
      final PatternExpr parsed = parsePattern(scanner);
      expect(
          parsed,
          equals(new PatternExpr(
              new Property([label1], Cardinality.singular, [label4]), [
            new PatternExpr(
                new Property([label2], Cardinality.singular, [label5])),
            new PatternExpr(new Property([label3], Cardinality.singular))
          ])));
      expect(parsed.toString(), equals(input));
    });
  });
}
