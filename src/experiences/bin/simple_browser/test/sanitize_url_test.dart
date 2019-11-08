// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_logger/logger.dart';

// ignore_for_file: implementation_imports
import 'package:simple_browser/src/utils/sanitize_url.dart';

void main() {
  setupLogger(name: 'sanitize_url_test');

  void urlSanitizationTest(String testUrl, String expectedResult) {
    String testResult = sanitizeUrl(testUrl);

    expect(testResult, expectedResult,
        reason: 'expected "$expectedResult" but actually "$testResult".');
  }

  test('URL Sanitization: URLs with supported schemes.', () {
    // With a URL that causes Format Exception to Dart's Uri.parse().
    urlSanitizationTest('http ://wrongformat',
        'https://www.google.com/search?q=http+%3A%2F%2Fwrongformat');

    // With a supported scheme(https) and a valid TLD(com).
    urlSanitizationTest('https://google.com', 'https://google.com');

    // With a supported scheme(http) and a valid TLD(org).
    urlSanitizationTest('http://wikipedia.org', 'http://wikipedia.org');

    // With a supported scheme(chrome) and a valid URL.
    urlSanitizationTest('chrome://gpu', 'chrome://gpu');

    // With a supported scheme(https) and an invalid TLD.
    urlSanitizationTest('https://google.cooom', 'https://google.cooom');

    // localhost with a port number
    urlSanitizationTest('localhost:8000', 'localhost:8000');
  });

  test('URL Sanitization: URLs without schemes.', () {
    const String googleSearchUrl = 'https://www.google.com/search?q=';

    // With a valid host pattern and a valid TLD.
    urlSanitizationTest('flutter.dev', 'https://flutter.dev');

    // With a valid host pattern and a valid TLD and a path
    urlSanitizationTest('flutter.dev/clock', 'https://flutter.dev/clock');

    // With an invalid host pattern and a valid TLD.
    urlSanitizationTest(
        'flu#%*tter.dev', '${googleSearchUrl}flu%23%25%2Atter.dev');

    // With a valid host pattern and an invalid TLD.
    urlSanitizationTest('google.cooom', '${googleSearchUrl}google.cooom');

    // With a keyword.
    urlSanitizationTest('fuchsia', '${googleSearchUrl}fuchsia');

    // With a valid ip address.
    urlSanitizationTest('255.111.18.1', 'https://255.111.18.1');

    // With a valid ip address and a path
    urlSanitizationTest('200.102.11.9/hello', 'https://200.102.11.9/hello');

    // With an invalid ip address (Each number must be a value between 0 - 255).
    urlSanitizationTest('955.111.18.1', '${googleSearchUrl}955.111.18.1');

    // With an invalid ip address (The address must consist of 4 numbers).
    urlSanitizationTest('255.111.18', '${googleSearchUrl}255.111.18');

    // With an invalid ip address (Spaced not allowed).
    urlSanitizationTest('255.111.18. 1', '${googleSearchUrl}255.111.18.+1');

    // With an invalid ip address (Non-digit characters except '.' not allowed).
    urlSanitizationTest('255.111.18.*1', '${googleSearchUrl}255.111.18.%2A1');
  });
}
