// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:html/dom.dart' as dom;

import '../common_util.dart';
import '../queries/index.dart';
import 'ast.dart';

dom.Element hr() => dom.Element.tag('hr');
dom.Element h1(dom.Node content) => dom.Element.tag('h1')..append(content);
dom.Element h2(dom.Node content) => dom.Element.tag('h2')..append(content);
dom.Element h3(dom.Node content) => dom.Element.tag('h3')..append(content);
dom.Element div(dom.Node content) => dom.Element.tag('div')..append(content);
dom.Element divChildren(Iterable<dom.Node> content) =>
    dom.Element.tag('div')..nodes.addAll(content);
dom.Element ul(Iterable<dom.Node> content) =>
    dom.Element.tag('ul')..nodes.addAll(content);
dom.Element li(dom.Node content) => dom.Element.tag('li')..append(content);
dom.Element liChildren(Iterable<dom.Node> content) =>
    dom.Element.tag('li')..nodes.addAll(content);
dom.Element span(dom.Node content) => dom.Element.tag('span')..append(content);
dom.Element spanChildren(Iterable<dom.Node> content) =>
    dom.Element.tag('span')..nodes.addAll(content);
dom.Node text(String content) => dom.Text(content);

/// A renderer into HTML, supporting any level of nested nodes.
class HtmlRenderer extends Renderer {
  // ignore: prefer_interpolation_to_compose_strings
  static final String _css = '''
html {
  font-size: 100%;
}

body {
  font-family: Noto, Roboto, Arial, Helvetica, sans-serif;
  font-size: 1.0rem;
  background-color: white;
  color: black;
}

ul {
  margin-left: 0;
  padding-left: 1rem;
}

ul ul {
  padding-left: 2rem;
  border-left: 3px solid #f77;
  background-color: #f1f1f1;
  list-style-type: square;
  list-style-position: outside;
}

ul ul ul {
  padding-left: 3rem;
}

body>ul>li div:last-child {
  margin-bottom: 0.8rem;
}

/* Make deeply nested items smaller */
body>ul>li li {
  font-size: 0.8rem;
}
''' +
      Color.values.map((c) => '''
.${c.className} {
  color: ${c.colorName};
  font-weight: bold;
}
''').join('\n');

  @override
  void render(StringSink output, Iterable<Query> queries) {
    final document = dom.Document.html('''
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
</head>
</html>
''');
    document.head.append(document.createElement('style')..innerHtml = _css);
    final body = document.body;
    bool first = true;
    for (final query in queries) {
      if (!first) {
        body.append(hr());
      }
      first = false;
      body
        ..append(h2(text(query.name)))
        ..append(div(text(query.getDescription())));
      final result = query.distill().export();
      body.append(ul([
        for (final node in result) liChildren(_renderNode(node, indent: 0))
      ]));
    }
    output.writeln(document.outerHtml);
  }

  Iterable<dom.Node> _renderNode(AnyNode node, {int indent = 0}) => [
        if (node.title != null) _renderTitle(node.title),
        if (indent == 0)
          for (final child in node.children ?? [])
            divChildren(_renderNode(child, indent: indent + 1))
        else if ((node.children ?? []).isNotEmpty)
          ul([
            for (final child in node.children)
              ..._renderNode(child, indent: indent + 1).map(li)
          ])
      ];

  dom.Node _renderTitle(Title title) {
    if (title is StringPiece) {
      // Dart analysis failed to narrow the type here..
      // ignore: avoid_as
      return _renderStringPiece(title as StringPiece);
    } else if (title is UniqueSymbolSizeRecord) {
      return spanChildren([
        _renderStringPiece(title.name),
        text(': ${formatSize(title.tally.size)} (${title.tally.size}), '
            '${title.tally.count} total symbols'),
        div(spanChildren([
          text('In categories: '),
          ...title.categories.map(_renderStringPiece).expand((e) => [
                e,
                text(' '),
              ]),
        ])),
        if (title.rustCrates.isNotEmpty)
          div(spanChildren([
            text('In rust crates: '),
            ...title.rustCrates.map(_renderStringPiece).expand((e) => [
                  e,
                  text(' '),
                ]),
          ])),
      ]);
    } else if (title is SizeRecord) {
      return spanChildren([
        _renderStringPiece(title.name),
        text(': ${formatSize(title.tally.size)} (${title.tally.size}), '
            '${title.tally.count} total symbols')
      ]);
    } else {
      throw Exception('Unsupported $title');
    }
  }

  dom.Node _renderStringPiece(StringPiece piece) {
    if (piece is AddColor) {
      return _colorize(
          spanChildren(piece.details.map(_renderStringPiece)), piece.color);
    } else if (piece is StyledString) {
      return spanChildren(piece.details.map(_renderStringPiece));
    } else if (piece is Plain) {
      return text(piece.text);
    } else {
      throw Exception('Unsupported $piece');
    }
  }

  dom.Element _colorize(dom.Element str, Color color) =>
      str..classes.add(color.className);
}

extension _css on Color {
  String get className => 'codesize-color-$colorName';

  // All codepaths in the switch will always return.
  String get colorName {
    switch (this) {
      case Color.gray:
        return 'gray';
      case Color.green:
        return 'green';
      case Color.white:
        // Since terminal background is black, but webpage is white,
        // we'll swap around the black and white color in HTML.
        return 'black';
    }
    throw Exception('Unreachable');
  }
}
