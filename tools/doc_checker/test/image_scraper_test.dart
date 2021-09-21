// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:doc_checker/image_scraper.dart';
import 'package:test/test.dart';

void main() {
  group('doc_checker image_scraper tests', () {
    test('no images found in plain text', () {
      final List<String> lines = ['this text has no links', 'same here'];
      expect(ImageScraper().scrapeLines(lines).isEmpty, isTrue);
    });

    test('images get scraped', () {
      final List<String> lines = [
        'images are scraped with alttext ![text](image.png)',
        'images are scraped with no alttext ![](no-alt.png)',
      ];
      Iterable<ImageData> images = ImageScraper().scrapeLines(lines);
      expect(images.elementAt(0).alt, equals('text'));
      expect(images.elementAt(0).src, equals('image.png'));
      expect(images.elementAt(1).alt, equals(''));
      expect(images.elementAt(1).src, equals('no-alt.png'));
    });
  });
}
