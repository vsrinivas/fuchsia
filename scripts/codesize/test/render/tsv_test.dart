// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'package:codesize/queries/index.dart';
import 'package:codesize/queries/mock_query.dart';
import 'package:codesize/render/ast.dart';
import 'package:codesize/render/tsv.dart';

void main() {
  final renderer = TsvRenderer();

  group('TsvRenderer', () {
    test('One Level', () {
      final output = StringBuffer();
      renderer.render(output, [
        MockQuery([
          Node.plain('[Title 1]'),
          Node.plain('[Title 2]'),
        ])
      ]);
      expect(
          output.toString(),
          equals('Details\n'
              '[Title 1]\n'
              '[Title 2]\n'));
    });

    test('Two Levels', () {
      final output = StringBuffer();
      renderer.render(output, [
        MockQuery([
          Node<SizeRecord>(
              title: SizeRecord(
                  name: StyledString.plain('[Title 1]'), tally: Tally(0, 42)),
              children: [Node.plain('[Child]')]),
          // This is of a different type, and should not be printed.
          Node.plain('[Title 2]'),
        ])
      ]);
      expect(
          output.toString(),
          equals('Name\tSize\tRaw Size\tNum Symbols\n'
              '[Title 1]\t0 B\t0\t42\n'));
    });

    test('Three Levels', () {
      final output = StringBuffer();
      renderer.render(output, [
        MockQuery([
          Node<SizeRecord>(
              title: SizeRecord(
                  name: StyledString.plain('[Title 1]'), tally: Tally(0, 42)),
              // These children should not be printed.
              children: [
                Node<StyledString>(
                    title: StyledString.plain('[Child]'),
                    children: [Node.plain('[Nested Child]')])
              ]),
          Node.plain('[Title 2]'),
        ])
      ]);
      expect(
          output.toString(),
          equals('Name\tSize\tRaw Size\tNum Symbols\n'
              '[Title 1]\t0 B\t0\t42\n'));
    });
  });
}
