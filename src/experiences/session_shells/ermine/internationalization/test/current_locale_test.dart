// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

// ignore_for_file: implementation_imports
import 'package:internationalization/current_locale.dart';

void main() {
  testWidgets('Check en_US from subtags', (tester) async {
    final CurrentLocale current = CurrentLocale(
        Locale.fromSubtags(languageCode: 'en', countryCode: 'US'));
    expect(current.unicode(), 'en_US');
    expect(current.value.toString(), 'en_US');
  });
  testWidgets('Check sr_RS from subtags', (tester) async {
    final CurrentLocale current = CurrentLocale(
        Locale.fromSubtags(languageCode: 'sr', countryCode: 'RS'));
    expect(current.unicode(), 'sr_RS');
    expect(current.value.toString(), 'sr_RS');
  });
  testWidgets('Check sr from constructor', (tester) async {
    final CurrentLocale current = CurrentLocale(Locale('sr'));
    expect(current.unicode(), 'sr');
    expect(current.value.toString(), 'sr');
  });
  testWidgets('Check sr_RS from constructor', (tester) async {
    final CurrentLocale current = CurrentLocale(Locale('sr_RS'));
    expect(current.unicode(), 'sr_RS');
    expect(current.value.toString(), 'sr_RS');
  });
}
