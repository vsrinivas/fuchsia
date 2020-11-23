// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:test/test.dart';
import 'package:codesize/queries/index.dart';
import 'package:codesize/queries/mock_query.dart';
import 'package:codesize/render/ast.dart';
import 'package:codesize/run_queries.dart';
import 'package:codesize/types.dart';

import 'testing_util.dart';

class SimpleQuery extends MockQuery {
  SimpleQuery(List<AnyNode> nodes) : super(nodes);

  @override
  void addReport(Report report) {
    if (report.context.name != 'libasync')
      throw Exception('Incorrect name ${report.context.name}');
    // These values were taken from libasync-default.so.bloaty_report_pb
    if (report.compileUnits.length != 15)
      throw Exception(
          'Incorrect compileUnits.length ${report.compileUnits.length}');
    count += 1;
  }

  @override
  void mergeWith(Iterable<Query> others) {
    for (final other in others) {
      if (other is SimpleQuery) count += other.count;
    }
  }

  int count = 0;
}

void main() {
  // See the `//scripts/codesize:bloaty_reports` target in `BUILD.gn`.
  Directory testData = locateTestData();
  Query mock() => SimpleQuery([]);
  group('QueryRunner', () {
    test('Empty', () async {
      await QueryRunner([mock], numConcurrency: 1).join();
      await QueryRunner([mock], numConcurrency: 10).join();
    });

    test('Run one query', () async {
      final runner = QueryRunner([mock], numConcurrency: 2);
      await runner.addReport(AnalysisItem(
          name: 'libasync',
          path: '${testData.path}/libasync-default.so.bloaty_report_pb'));
      await runner.join();
      expect(runner.queries.first, isA<SimpleQuery>());
      // ignore: avoid_as
      expect((runner.queries.first as SimpleQuery).count, equals(1));
    });

    test('Run many queries', () async {
      const count = 100;
      final runner = QueryRunner([mock], numConcurrency: 2);
      await Future.wait(Iterable<int>.generate(count).map((i) =>
          runner.addReport(AnalysisItem(
              name: 'libasync',
              path: '${testData.path}/libasync-default.so.bloaty_report_pb'))));
      await runner.join();
      expect(runner.queries.first, isA<SimpleQuery>());
      // ignore: avoid_as
      expect((runner.queries.first as SimpleQuery).count, equals(count));
    });
  });
}
