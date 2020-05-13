// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:doc_checker/link_scraper.dart';
import 'package:test/test.dart';

void main() {
  group('doc_checker link_scraper tests', () {
    test('no links found in plain text', () {
      final List<String> lines = ['this text has no links', 'same here'];
      expect(LinkScraper().scrapeLines(lines).isEmpty, isTrue);
    });

    test('links get scraped', () {
      final List<String> lines = [
        'this text has no links',
        'this one [does](link.md).',
        'but not *this one*',
        'images are scraped ![text](image.png)'
      ];
      Iterable<String> links = LinkScraper().scrapeLines(lines);
      expect(links, hasLength(2));
      expect(links.first, equals('link.md'));
      expect(links.last, equals('image.png'));
    });
  });
}
