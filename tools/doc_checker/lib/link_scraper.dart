// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:markdown/markdown.dart';
import 'package:meta/meta.dart';

/// Scrapes links in a markdown document.
class LinkScraper {
  /// Extracts links from the given [file].
  Iterable<String> scrape(String file) {
    return scrapeLines(File(file).readAsLinesSync());
  }

  /// Extracts links from the given list of [lines].
  @visibleForTesting
  Iterable<String> scrapeLines(List<String> lines) {
    final List<Node> nodes = Document().parseLines(lines);
    final _Visitor visitor = _Visitor();
    for (Node node in nodes) {
      node.accept(visitor);
    }
    return visitor.links;
  }
}

class _Visitor implements NodeVisitor {
  static const String _key = 'href';
  static const String _imgSrc = 'src';
  static const String _imgTag = 'img';

  final Set<String> links = <String>{};

  @override
  bool visitElementBefore(Element element) {
    if (element.attributes.containsKey(_key)) {
      links.add(element.attributes[_key]);
    } else if (element.tag == _imgTag &&
        element.attributes.containsKey(_imgSrc)) {
      links.add(element.attributes[_imgSrc]);
    }
    return true;
  }

  @override
  void visitElementAfter(Element element) {}

  @override
  void visitText(Text text) {}
}
