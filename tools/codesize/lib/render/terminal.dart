// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'package:io/ansi.dart' as ansi;

import '../common_util.dart';
import '../queries/index.dart';
import 'ast.dart';

/// A renderer into rich terminal output, supporting any level of nested nodes.
class TerminalRenderer extends Renderer {
  TerminalRenderer({this.supportsControlCharacters = true});

  @override
  void render(StringSink output, Iterable<Query> queries) {
    ansi.overrideAnsiOutput(supportsControlCharacters, () {
      for (final query in queries) {
        final separator = '─' * (80 - query.name.length - 5);
        output.writeln('── ${ansi.styleBold.wrap(query.name)} $separator');
        final result = query.distill().export();
        for (final node in result) {
          _renderNode(output, node, indent: 0);
        }
        output.writeln();
      }
    });
  }

  void _renderNode(StringSink output, AnyNode node, {int indent = 0}) {
    if (node.title != null) {
      if (indent == 0) {
        output.write('└─ ');
      } else if (indent > 0) {
        output
          ..write('│')
          ..write('  ' * indent);
      }
      _renderTitle(output, node.title);
      output.writeln();
    }
    for (final child in node.children ?? []) {
      _renderNode(output, child, indent: indent + 1);
    }
  }

  void _renderTitle(StringSink output, Title title) {
    if (title is StringPiece) {
      // Dart analysis failed to narrow the type here..
      // ignore: avoid_as
      output.write(_renderStringPiece(title as StringPiece));
    } else if (title is UniqueSymbolSizeRecord) {
      output
        ..write('${_renderStringPiece(title.name)}: '
            '${formatSize(title.tally.size)} (${title.tally.size}), '
            '${title.tally.count} total symbols')
        ..writeln()
        ..write('│  In categories: '
            '${title.categories.map(_renderStringPiece).join(' ')}')
        ..write(title.rustCrates.isEmpty
            ? ''
            : '\n│  In rust crates: '
                '${title.rustCrates.map(_renderStringPiece).join(' ')}');
    } else if (title is SizeRecord) {
      output.write('${_renderStringPiece(title.name)}: '
          '${formatSize(title.tally.size)} (${title.tally.size}), '
          '${title.tally.count} total symbols');
    } else {
      throw Exception('Unsupported $title');
    }
  }

  String _renderStringPiece(StringPiece piece) {
    if (piece is AddColor) {
      return ansi.wrapWith(piece.details.map(_renderStringPiece).join(''), [
        (() {
          switch (piece.color) {
            case Color.white:
              return ansi.white;
            case Color.green:
              return ansi.green;
            case Color.gray:
              return ansi.darkGray;
          }
        })(),
        ansi.styleBold,
      ]);
    } else if (piece is StyledString) {
      return piece.details.map(_renderStringPiece).join('');
    } else if (piece is Plain) {
      return piece.text;
    } else {
      throw Exception('Unsupported $piece');
    }
  }

  final bool supportsControlCharacters;
}
