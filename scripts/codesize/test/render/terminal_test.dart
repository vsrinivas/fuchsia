// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'package:codesize/queries/index.dart';
import 'package:codesize/queries/mock_query.dart';
import 'package:codesize/render/ast.dart';
import 'package:codesize/render/terminal.dart';

void main() {
  final renderer = TerminalRenderer(supportsControlCharacters: false);

  group('TerminalRenderer', () {
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
          equals(
              '── MockQuery ──────────────────────────────────────────────────────────────────\n'
              '└─ [Title 1]\n'
              '└─ [Title 2]\n'
              '\n'));
    });

    test('Two Levels', () {
      final output = StringBuffer();
      renderer.render(output, [
        MockQuery([
          Node<SizeRecord>(
              title: SizeRecord(
                  name: StyledString.plain('[Title 1]'), tally: Tally(0, 42)),
              children: [Node.plain('[Child]')]),
          Node.plain('[Title 2]'),
        ])
      ]);
      expect(
          output.toString(),
          equals(
              '── MockQuery ──────────────────────────────────────────────────────────────────\n'
              '└─ [Title 1]: 0 B (0), 42 total symbols\n'
              '│  [Child]\n'
              '└─ [Title 2]\n'
              '\n'));
    });

    test('Three Levels', () {
      final output = StringBuffer();
      renderer.render(output, [
        MockQuery([
          Node<SizeRecord>(
              title: SizeRecord(
                  name: StyledString.plain('[Title 1]'), tally: Tally(0, 42)),
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
          equals(
              '── MockQuery ──────────────────────────────────────────────────────────────────\n'
              '└─ [Title 1]: 0 B (0), 42 total symbols\n'
              '│  [Child]\n'
              '│    [Nested Child]\n'
              '└─ [Title 2]\n'
              '\n'));
    });
  });
}
