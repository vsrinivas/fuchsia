// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_intl/fidl_async.dart' as fintl;
import 'package:fidl_fuchsia_settings/fidl_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';
import 'package:settings/settings.dart';

const List<TimeZoneInfo> timeZones = [
  TimeZoneInfo(zoneId: 'Test/A'),
  TimeZoneInfo(zoneId: 'Test/B'),
];

void main() {
  test('Change Timezone', () async {
    var response = 'tz1';

    final intlSettingsProxy = MockIntlProxy();
    final intlSettingsProxyController = MockIntlProxyController();

    when(intlSettingsProxy.ctrl).thenReturn(intlSettingsProxyController);
    when(intlSettingsProxy.watch())
        .thenAnswer((_) => Future<IntlSettings>.value(IntlSettings(
              timeZoneId: fintl.TimeZoneId(id: response),
            )));

    TimeZone timeZone = TimeZone(
        intlSettingsService: intlSettingsProxy,
        timeZonesProvider: () => Future.value(timeZones));
    final specA = await timeZone.getSpec();
    expect(specA.groups?.first.values?.first.text?.text, response);

    response = 'tz2';
    // Wait one event cycle for the change.
    await timeZone.getSpec();
    final specB = await timeZone.getSpec();
    expect(specB.groups?.first.values?.first.text?.text, response);

    timeZone.dispose();
  });
}

// Mock classes.
class MockIntlProxy extends Mock implements IntlProxy {}

class MockIntlProxyController extends Mock
    implements AsyncProxyController<Intl> {}
