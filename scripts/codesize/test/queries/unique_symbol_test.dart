// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:test/test.dart';

import 'package:codesize/common_util.dart';
import 'package:codesize/render/ast.dart';
import 'package:codesize/report.pb.dart' as bloaty_report;
import 'package:codesize/types.dart';
import 'package:codesize/queries/index.dart';
import 'package:codesize/queries/unique_symbol.dart';

import '../testing_util.dart';

void main() {
  Directory testData = locateTestData();
  test('unique symbols in libasync-default.so', () async {
    final report = Report.fromBytes(
        'libasync',
        await File('${testData.path}/libasync-default.so.bloaty_report_pb')
            .readAsBytes());
    final query = UniqueSymbolQuery()..addReport(report);
    expect(
        query.toString(),
        equals('[Unmapped]\n'
            '[ELF Headers]\n'
            '[LOAD #1 [R]]\n'
            'g_default\n'
            'async_get_default_dispatcher\n'
            'async_set_default_dispatcher\n'
            '[section .dynamic]\n'
            '[section .shstrtab]\n'
            '[section .dynstr]\n'
            '[section .gnu.hash]\n'
            '[section .eh_frame]\n'
            '[section .dynsym]\n'
            '[section .note.gnu.build-id]\n'
            '[section .rela.dyn]\n'
            '[section .got]\n'
            '[section .eh_frame_hdr]\n'
            '[section .tbss]'));
  });

  test('two synthesized symbols', () {
    final protobufReport = bloaty_report.Report()
      ..compileUnits.add(bloaty_report.CompileUnit()
        ..name = 'test.c'
        ..symbols.add(bloaty_report.Symbol.create()
          ..sizes = (bloaty_report.SizeInfo.create()..fileActual = 100)
          ..name = 'foo')
        ..symbols.add(bloaty_report.Symbol.create()
          ..sizes = (bloaty_report.SizeInfo.create()..fileActual = 42)
          ..name = 'bar'));
    final report = Report.fromBloaty('test', protobufReport);
    final query = UniqueSymbolQuery(hideUnknown: false)..addReport(report);

    final List<AnyNode> actual = query.distill().export().toList();
    final List<AnyNode> expected = [
      Node(
          title: UniqueSymbolSizeRecord(
              name: AddColor.white(Plain('foo')),
              tally: Tally(100, 1),
              categories: [AddColor.gray(Plain('Uncategorized'))],
              rustCrates: []),
          children: []),
      Node(
          title: UniqueSymbolSizeRecord(
              name: AddColor.white(Plain('bar')),
              tally: Tally(42, 1),
              categories: [AddColor.gray(Plain('Uncategorized'))],
              rustCrates: []),
          children: []),
    ].toList();
    expect(actual.length, equals(expected.length));
    for (final pair in zip(actual, expected)) {
      expect(pair.a, equals(pair.b));
    }
  });
}
