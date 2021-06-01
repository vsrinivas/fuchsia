// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports
import 'package:fidl/fidl.dart' as fidl;
import 'package:fidl_fuchsia_intl/fidl_async.dart' as fidl_intl;
import 'package:fuchsia_internationalization_flutter/internationalization.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

void main() {
  setupLogger();

  late MockPropertyProvider mockPropertyProvider;
  late LocaleSource source;

  setUp(() async {
    mockPropertyProvider = MockPropertyProvider();
    source = LocaleSource(mockPropertyProvider);
  });

  const _someProfile = fidl_intl.Profile(
    locales: [
      fidl_intl.LocaleId(id: 'sr_RS'),
      fidl_intl.LocaleId(id: 'en_US'),
    ],
  );
  const _otherProfile = fidl_intl.Profile(
    locales: [
      fidl_intl.LocaleId(id: 'ru_RU'),
      fidl_intl.LocaleId(id: 'sr_RS'),
      fidl_intl.LocaleId(id: 'en_US'),
    ],
  );
  const _thirdProfile = fidl_intl.Profile(
    locales: [
      fidl_intl.LocaleId(id: 'nl_NL'),
      fidl_intl.LocaleId(id: 'ru_RU'),
      fidl_intl.LocaleId(id: 'sr_RS'),
      fidl_intl.LocaleId(id: 'en_US'),
    ],
  );

  test('Test initial locale', () async {
    when(mockPropertyProvider.getProfile())
        .thenAnswer((_) => Future.value(_someProfile));
    when(mockPropertyProvider.onChange)
        .thenAnswer((_) => Stream.fromIterable([]));

    expect((await source.stream().first).toString(), equals('sr_RS'));
  });

  test('Test initial locale gets an exception and returns default', () async {
    when(mockPropertyProvider.getProfile())
        .thenThrow(fidl.FidlError('I hate you!'));
    when(mockPropertyProvider.onChange)
        .thenAnswer((_) => Stream.fromIterable([]));
    expect((await source.stream().first).toString(), equals('en_US'));
  });

  test('Test obtaining updates on change', () async {
    // This is how you get different return values for repeated calls of the
    // same method.  It is important that 'answers is captured outside of the
    // answer closure.  See
    // https://stackoverflow.com/questions/53896225/chain-multiple-calls-with-same-arguments-to-return-different-results
    var answers = [
      Future.value(_someProfile),
      Future.value(_otherProfile),
      Future.value(_thirdProfile),
    ];
    when(mockPropertyProvider.getProfile())
        .thenAnswer((_) => answers.removeAt(0));
    when(mockPropertyProvider.onChange)
        .thenAnswer((_) => Stream.fromIterable(['1', '2']));

    expect(await source.stream().join(','), equals('sr_RS,ru_RU,nl_NL'));
  });
}

class MockPropertyProvider extends Mock implements fidl_intl.PropertyProvider {}
