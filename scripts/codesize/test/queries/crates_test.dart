// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:test/test.dart';
import 'package:codesize/render/ast.dart';
import 'package:codesize/report.pb.dart' as bloaty_report;
import 'package:codesize/types.dart';
import 'package:codesize/queries/crates.dart';

import '../testing_util.dart';

void main() {
  Directory testData = locateTestData();
  test('libasync-default.so has no crates', () async {
    final report = Report.fromBytes(
        'libasync',
        await File('${testData.path}/libasync-default.so.bloaty_report_pb')
            .readAsBytes());
    final query = CratesQuery()..addReport(report);
    expect(query.toString(), equals('Nothing selected'));
    expect(query.distill().export(), equals([Node.plain('Nothing selected')]));
  });

  test('single crate', () {
    final protobufReport = bloaty_report.Report()
      ..compileUnits.add(bloaty_report.CompileUnit()
        ..symbols.add(bloaty_report.Symbol.create()
          ..sizes = (bloaty_report.SizeInfo.create()..fileActual = 42)
          ..maybeRustCrate = 'foo'));
    final report = Report.fromBloaty('test', protobufReport);
    final query = CratesQuery()..addReport(report);
    expect(query.toString(), equals('foo: 42 B (42), count: 1'));
  });
}
