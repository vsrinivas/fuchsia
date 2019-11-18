// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_deprecatedtimezone/fidl_async.dart';
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';
import 'package:settings/settings.dart';

void main() {
  test('Timezone', () async {
    final timezoneProxy = MockTimezoneProxy();
    final binding = MockBinding();

    when(timezoneProxy.getTimezoneId())
        .thenAnswer((_) => Future<String>.value('Foo'));

    TimeZone timezone = TimeZone(timezone: timezoneProxy, binding: binding);
    final spec = await timezone.getSpec();
    expect(spec.groups.first.values.first.text.text == 'Foo', true);
  });

  test('Change Timezone', () async {
    final timezoneProxy = MockTimezoneProxy();
    final binding = MockBinding();

    when(timezoneProxy.getTimezoneId())
        .thenAnswer((_) => Future<String>.value('Foo'));

    TimeZone timezone = TimeZone(timezone: timezoneProxy, binding: binding);
    await timezone.getSpec();

    final spec = await timezone.getSpec(Value.withText(TextValue(
      text: 'US/Eastern',
      action: QuickAction.submit.$value,
    )));

    expect(spec.groups.first.values.first.text.text == 'US/Eastern', true);
  });
}

// Mock classes.
class MockTimezoneProxy extends Mock implements TimezoneProxy {}

class MockBinding extends Mock implements TimezoneWatcherBinding {}
