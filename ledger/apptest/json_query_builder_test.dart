// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_core/uuid.dart';
import 'package:ledger/src/json_query_builder.dart';
import 'package:modular_services/ledger/ledger.mojom.dart';
import 'package:mojo/application.dart';
import 'package:mojo_apptest/apptest.dart';

void jsonQueryBuilderTests(Application app, String url) {
  test('Test query builder', () {
    List<LabelUri> labels = [
      new LabelUri()..uri = "A",
      new LabelUri()..uri = "B",
    ];
    final String testId = new Uuid.random().toBase64();

    final JsonQueryBuilder queryBuilder = new JsonQueryBuilder()
      ..addExpectedLabels(labels);
    final String labelsString = '"labels":["A","B"]';
    expect(queryBuilder.buildQuery(), '{$labelsString}');

    queryBuilder.setExpectedStart(new NodeId()..id = testId);
    final String startString = '"start":"$testId"';

    expect(queryBuilder.buildQuery(), '{$labelsString,$startString}');
    queryBuilder.setExpectedEnd(new NodeId()..id = testId);
    final String endString = '"end":"$testId"';
    expect(
        queryBuilder.buildQuery(), '{$labelsString,$startString,$endString}');

    queryBuilder.setExpectedDeleted(deleted: false);
    final String deletedString = '"deleted":false';
    expect(queryBuilder.buildQuery(),
        '{$labelsString,$startString,' + '$endString,$deletedString}');
  });
}
