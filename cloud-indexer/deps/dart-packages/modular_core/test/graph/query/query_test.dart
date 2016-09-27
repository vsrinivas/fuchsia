// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'package:modular_core/graph/query/query.dart';
import 'package:parser/expression.dart';

void main() {
  group('Factory:', () {
    test('From PatternExpr', () {
      // Just a basic test to make sure that the factory fromPatternExpr works.
      // Correct conversion testing takes place in convert_test.dart.
      expect(
          new GraphQuery.fromPatternExpr(new PatternExpr(new Property(
              new Set<Label>.from([new Label.fromUri(Uri.parse('foo'))])))),
          equals(new GraphQuery(['foo'], isRequired: true)));
    });
    test('From String', () {
      // Again we only have to test that GraphQuery is wired together with
      // the parser in such a way that it will come back with a GraphQuery.
      expect(
          new GraphQuery.fromString('internal:foo -> internal:bar'),
          equals(new GraphQuery(['internal:foo'],
              isRequired: true,
              childConstraints: [
                new GraphQuery(['internal:bar'], isRequired: true)
              ])));
    });
  });
  test('fromString().toString() Round-trip', () {
    // final String query = 'internal:foo* -> i:cfoo{i:bar?, i:baz+}';
    final List<String> queries = [
      '_ -> internal:foo -> (internal:bar internal:baz)?',
      'internal:optional? {internal:c1*, internal:c2+}',
      'internal:hasrep <internal:rep1, internal:rep2> -> internal:child',
    ];
    queries.forEach((String query) {
      print(query);
      expect(new GraphQuery.fromString(query).toString(), equals(query));
    });
  });
}
