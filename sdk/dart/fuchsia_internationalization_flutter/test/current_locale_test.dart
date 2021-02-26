// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

// ignore_for_file: implementation_imports
import 'package:fuchsia_internationalization_flutter/internationalization.dart';

void main() {
  test('Check en_US from subtags', () async {
    final CurrentLocale current = CurrentLocale(
        Locale.fromSubtags(languageCode: 'en', countryCode: 'US'));
    expect(current.unicode(), 'en_US');
    expect(current.value.toString(), 'en_US');
  });
  test('Check sr_RS from subtags', () async {
    final CurrentLocale current = CurrentLocale(
        Locale.fromSubtags(languageCode: 'sr', countryCode: 'RS'));
    expect(current.unicode(), 'sr_RS');
    expect(current.value.toString(), 'sr_RS');
  });
  test('Check sr from constructor', () async {
    final CurrentLocale current = CurrentLocale(Locale('sr'));
    expect(current.unicode(), 'sr');
    expect(current.value.toString(), 'sr');
  });
  test('Check sr_RS from constructor', () async {
    final CurrentLocale current = CurrentLocale(Locale('sr_RS'));
    expect(current.unicode(), 'sr_RS');
    expect(current.value.toString(), 'sr_RS');
  });
  // Test of an invalid Locale() is conspicuously absent.  This is because
  // Dart does not allow an initialization of an invalid locale.  E.g. trying:
  // var locale = Locale.fromSubtags(languageCode: '', countryCode: '')
  // will crash a program.
}
