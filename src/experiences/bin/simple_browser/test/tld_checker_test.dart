// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fuchsia_logger/logger.dart';
import 'package:test/test.dart';

// ignore_for_file: implementation_imports
import 'package:simple_browser/src/utils/tld_checker.dart';
import 'package:simple_browser/src/utils/tlds_provider.dart';

void main() {
  setupLogger(name: 'tld_checker_test');
  test('TLD Validity check test with local TLD list.', () {
    expect(TldChecker().isValid('dev'), true, reason: '"dev" should be valid.');
    expect(TldChecker().isValid('asdf'), false,
        reason: '"asdf" should be invalid.');
  });

  test('Fetch test with two valid TLDs and one comment line.', () async {
    String testData = '''
    # version 2019111500, Last Updated Fri Nov 15 07:07:01 2019 UTC
    AAA
    BBB
    ''';
    TldsProvider provider = TldsProvider()..data = testData;
    List<String> testTlds = await provider.fetchTldsList();
    TldChecker().prefetchTlds(testTlds: testTlds);

    expect(TldChecker().validTlds.length, 2,
        reason: 'The length of valid TLD list should be two.');
    expect(TldChecker().isValid('aaa'), true, reason: '"aaa" should be valid.');
    expect(TldChecker().isValid('bbb'), true, reason: '"bbb should be valid.');
  });

  test('Fetch test with one valid TLDs and multiple comment lines.', () async {
    String testData = '''
    # version 2019111500, Last Updated Fri Nov 15 07:07:01 2019 UTC
    # Lorem ipsum dolor sit amet, consectetur adipiscing elit.
    # Donec sed libero at lacus blandit scelerisque.
    # Aliquam at felis ac orci facilisis tincidunt.
    # Nullam eu justo faucibus, pretium urna et, pretium magna.
    # Duis faucibus elit id felis sollicitudin, quis fermentum neque convallis.
    QQQ
    ''';

    TldsProvider provider = TldsProvider()..data = testData;
    List<String> testTlds = await provider.fetchTldsList();
    TldChecker().prefetchTlds(testTlds: testTlds);

    expect(TldChecker().validTlds.length, 1,
        reason: 'The length of valid TLD list should be one.');
    expect(TldChecker().isValid('qqq'), true, reason: '"qqq" should be valid.');
  });
}
