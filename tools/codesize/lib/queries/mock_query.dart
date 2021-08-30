// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import '../render/ast.dart';
import '../types.dart';
import 'index.dart';

/// This class is only meant for testing.
class MockQuery implements Query, QueryReport {
  final List<AnyNode> nodes;

  MockQuery(this.nodes);

  @override
  void addReport(Report report) {
    throw UnimplementedError();
  }

  @override
  QueryReport distill() => this;

  @override
  String getDescription() => '[Description]';

  @override
  void mergeWith(Iterable<Query> others) {
    throw UnimplementedError();
  }

  @override
  String get name => 'MockQuery';

  @override
  Iterable<AnyNode> export() => nodes;
}
