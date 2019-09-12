// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:settings/settings.dart';

void main() {
  Cpu cpu;

  setUp(() async {
    cpu = Cpu();
  });

  tearDown(() async {
    cpu.dispose();
  });

  test('Cpu', () async {
    Spec spec = await cpu.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    GraphValue graph = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.graph)
        .first
        ?.graph;
    expect(graph, isNotNull);
    expect(graph?.value, greaterThan(0));
    expect(graph?.value, lessThan(1));

    cpu.dispose();
  });

  test('Verify Cpu Updates', () async {
    // Should receive cpu spec.
    Spec spec = await cpu.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Retain previous cpu value.
    GraphValue graph = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.graph)
        .first
        ?.graph;

    double previousValue = graph?.value;

    // allow time for cpu to update
    await Future.delayed(Duration(seconds: 1));

    // Should follow immediately by cpu spec.
    spec = await cpu.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm cpu value has updated within graph
    GraphValue newGraph = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.graph)
        .first
        ?.graph;
    expect(newGraph, isNotNull);
    expect(newGraph?.value, isNot(previousValue));
  });
}
