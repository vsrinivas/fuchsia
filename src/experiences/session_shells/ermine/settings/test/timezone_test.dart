// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_intl/fidl_async.dart';
import 'package:fidl_fuchsia_settings/fidl_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';
import 'package:settings/settings.dart';

void main() {
  MockIntlProxy intlSettingsProxy;
  MockIntlProxyController intlSettingsProxyController;

  setUp(() async {
    intlSettingsProxy = MockIntlProxy();
    intlSettingsProxyController = MockIntlProxyController();
    when(intlSettingsProxy.ctrl).thenReturn(intlSettingsProxyController);
  });

  test('Timezone', () async {
    when(intlSettingsProxy.watch())
        .thenAnswer((_) => Future<IntlSettings>.value(IntlSettings(
              timeZoneId: TimeZoneId(id: 'Foo'),
            )));

    TimeZone timeZone = TimeZone(intlSettingsService: intlSettingsProxy);
    final spec = await timeZone.getSpec();
    expect(spec.groups.first.values.first.text.text == 'Foo', true);

    timeZone.dispose();
  });

  test('Change Timezone', () async {
    var response = 'tz1';

    when(intlSettingsProxy.watch()).thenAnswer((_) {
      return Future<IntlSettings>.value(IntlSettings(
        timeZoneId: TimeZoneId(id: response),
      ));
    });

    TimeZone timeZone = TimeZone(intlSettingsService: intlSettingsProxy);
    final specA = await timeZone.getSpec();
    expect(specA.groups.first.values.first.text.text, 'tz1');

    response = 'tz2';
    // Wait one event cycle for the change.
    await timeZone.getSpec();
    final specB = await timeZone.getSpec();
    expect(specB.groups.first.values.first.text.text, 'tz2');

    timeZone.dispose();
  });
}

// Mock classes.
class MockIntlProxy extends Mock implements IntlProxy {}

class MockIntlProxyController extends Mock
    implements AsyncProxyController<Intl> {}
