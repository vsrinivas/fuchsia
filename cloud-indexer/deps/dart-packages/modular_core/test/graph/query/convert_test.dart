// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'package:modular_core/graph/query/convert.dart';
import 'package:modular_core/graph/query/query.dart';
import 'package:parser/cardinality.dart';
import 'package:parser/expression.dart';

void main() {
  final Function convert = patternExprToGraphQuery; // For convenience.

  test('One Component: Wildcard', () {
    expect(convert(new PatternExpr(new Property(null))),
        equals(new GraphQuery([], isRequired: true)));
  });

  test('One Component: Labels', () {
    expect(
        convert(new PatternExpr(new Property(
            new Set<Label>.from([new Label(Uri.parse('foo'), 'foo')])))),
        equals(new GraphQuery(['foo'], isRequired: true)));

    expect(
        convert(new PatternExpr(new Property(new Set<Label>.from([
          new Label(Uri.parse('foo'), 'foo'),
          new Label(Uri.parse('bar'), 'bar')
        ])))),
        equals(new GraphQuery(['foo', 'bar'], isRequired: true)));
  });

  test('One Component: Cardinality', () {
    // Optional
    expect(
        convert(new PatternExpr(new Property(
            new Set<Label>.from([new Label(Uri.parse('foo'), 'foo')]),
            Cardinality.optional))),
        equals(new GraphQuery(['foo'], isRequired: false)));

    // Repeated
    expect(
        convert(new PatternExpr(new Property(
            new Set<Label>.from([new Label(Uri.parse('foo'), 'foo')]),
            Cardinality.repeated))),
        equals(new GraphQuery(['foo'], isRequired: true, isRepeated: true)));

    // Optional repeated
    expect(
        convert(new PatternExpr(new Property(
            new Set<Label>.from([new Label(Uri.parse('foo'), 'foo')]),
            Cardinality.optionalRepeated))),
        equals(new GraphQuery(['foo'], isRequired: false, isRepeated: true)));
  });

  test('One Component: Representation Labels', () {
    expect(
        convert(new PatternExpr(new Property(
            new Set<Label>.from([new Label(Uri.parse('foo'), 'foo')]),
            null, /* cardinality */
            [
              new Label.fromUri(Uri.parse('value1')),
              new Label.fromUri(Uri.parse('value2'))
            ]))),
        equals(new GraphQuery(['foo'],
            valueLabels: ['value1', 'value2'], isRequired: true)));
  });

  test('Multiple Components: Labels', () {
    expect(
        convert(new PatternExpr(
            new Property(
                new Set<Label>.from([new Label.fromUri(Uri.parse('foo'))])),
            [
              new PatternExpr(new Property(
                  new Set<Label>.from([new Label.fromUri(Uri.parse('bar'))])))
            ])),
        equals(new GraphQuery(['foo'],
            isRequired: true,
            childConstraints: [
              new GraphQuery(['bar'], isRequired: true)
            ])));
  });

  test('Multiple Components: Representation Labels', () {
    // Representation labels can appear on any node in the PatternExpr tree.
    expect(
        convert(new PatternExpr(
            new Property(
                new Set<Label>.from([new Label.fromUri(Uri.parse('foo'))]),
                null, /* cardinality */
                [
                  new Label.fromUri(Uri.parse('foovalue1')),
                  new Label.fromUri(Uri.parse('foovalue2'))
                ]),
            [
              new PatternExpr(new Property(
                  new Set<Label>.from([new Label.fromUri(Uri.parse('bar'))]),
                  null, /* cardinality */
                  [
                    new Label.fromUri(Uri.parse('barvalue1')),
                    new Label.fromUri(Uri.parse('barvalue2'))
                  ]))
            ])),
        equals(new GraphQuery(['foo'],
            isRequired: true,
            valueLabels: ['foovalue1', 'foovalue2'],
            childConstraints: [
              new GraphQuery(['bar'],
                  isRequired: true, valueLabels: ['barvalue1', 'barvalue2'])
            ])));
  });

  test('Multiple Components: Complex', () {
    // Create a medium-complexity PatternExpr that looks a bit like this:
    //
    //   foo {bar<a>?, baz<b> -> bang* }
    expect(
        convert(new PatternExpr(
            new Property(
                new Set<Label>.from([new Label.fromUri(Uri.parse('foo'))])),
            [
              new PatternExpr(new Property(
                  new Set<Label>.from([new Label.fromUri(Uri.parse('bar'))]),
                  Cardinality.optional,
                  [new Label.fromUri(Uri.parse('a'))])),
              new PatternExpr(
                  new Property(
                      new Set<Label>.from(
                          [new Label.fromUri(Uri.parse('baz'))]),
                      null, /* cardinality */
                      [new Label.fromUri(Uri.parse('b'))]),
                  [
                    new PatternExpr(new Property(
                        new Set<Label>.from(
                            [new Label.fromUri(Uri.parse('bang'))]),
                        Cardinality.optionalRepeated))
                  ])
            ])),
        equals(new GraphQuery(['foo'],
            isRequired: true,
            childConstraints: [
              new GraphQuery(['bar'], isRequired: false, valueLabels: ['a']),
              new GraphQuery(['baz'],
                  isRequired: true,
                  valueLabels: ['b'],
                  childConstraints: [
                    new GraphQuery(['bang'],
                        isRepeated: true, isRequired: false)
                  ])
            ])));
  });

  // Path Expressions
  test('Convert path expressions to graph query', () {
    expect(
        pathExprToGraphQuery(new PathExpr([
          new Property(
              new Set<Label>.from([new Label(Uri.parse('foo'), 'foo')]),
              null, /* cardinality */
              [
                new Label.fromUri(Uri.parse('value1')),
                new Label.fromUri(Uri.parse('value2'))
              ]),
          new Property(
              new Set<Label>.from([new Label(Uri.parse('bar'), 'bar')]),
              null, /* cardinality */
              [
                new Label.fromUri(Uri.parse('value3')),
                new Label.fromUri(Uri.parse('value4'))
              ])
        ])),
        equals(new GraphQuery(['foo'],
            valueLabels: ['value1', 'value2'],
            isRequired: true,
            childConstraints: [
              new GraphQuery(['bar'],
                  valueLabels: ['value3', 'value4'], isRequired: true)
            ])));
  });
}
