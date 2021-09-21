// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io';

import 'package:markdown/markdown.dart';
import 'package:meta/meta.dart';

/// Holds the src and alt attributes for an image.
class ImageData {
  String src;
  String alt;
}

/// Scrapes image sources and alt text in a markdown document.
class ImageScraper {
  /// Extracts links from the given [file].
  Iterable<ImageData> scrape(String file) {
    return scrapeLines(File(file).readAsLinesSync());
  }

  /// Extracts links from the given list of [lines].
  @visibleForTesting
  Iterable<ImageData> scrapeLines(List<String> lines) {
    final List<Node> nodes = Document().parseLines(lines);
    final _Visitor visitor = _Visitor();
    for (Node node in nodes) {
      node.accept(visitor);
    }
    return visitor.images;
  }
}

class _Visitor implements NodeVisitor {
  static const String _imgAlt = 'alt';
  static const String _imgSrc = 'src';
  static const String _imgTag = 'img';

  final Set<ImageData> images = <ImageData>{};

  @override
  bool visitElementBefore(Element element) {
    if (element.tag == _imgTag && element.attributes.containsKey(_imgSrc)) {
      var img = ImageData()
        ..alt = element.attributes[_imgAlt]
        ..src = element.attributes[_imgSrc];
      images.add(img);
    }
    return true;
  }

  @override
  void visitElementAfter(Element element) {}

  @override
  void visitText(Text text) {}
}
