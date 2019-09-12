// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_ui_brightness/fidl_async.dart';
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';
import 'package:settings/settings.dart';

void main() {
  MockControl control;
  MockProxyController mockProxy;

  setUp(() async {
    control = MockControl();
    mockProxy = MockProxyController();
    when(control.ctrl).thenReturn(mockProxy);
  });

  tearDown(() async {
    verify(mockProxy.close()).called(1);
  });

  test('Brightness', () async {
    when(control.watchAutoBrightness()).thenAnswer((_) => Future.value(false));
    when(control.watchCurrentBrightness()).thenAnswer((_) => Future.value(0.8));
    final brightness = Brightness(control);

    // Should receive brightness spec.
    Spec spec = await brightness.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    ProgressValue progress = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.progress)
        .first
        ?.progress;
    expect(progress, isNotNull);
    expect(progress?.value, 0.8);

    brightness.dispose();
  });

  test('Change Brightness', () async {
    when(control.watchAutoBrightness()).thenAnswer((_) => Future.value(true));
    when(control.watchCurrentBrightness()).thenAnswer((_) => Future.value(0.5));

    // Should receive brightness spec.
    final brightness = Brightness(control);
    Spec spec = await brightness.getSpec();

    // Now change the brightness.
    brightness.update(Value.withProgress(ProgressValue(
      value: 0.3,
      action: Brightness.progressAction,
    )));

    verify(control.setManualBrightness(0.3));

    // Should follow immediately by brightness spec.
    spec = await brightness.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    ProgressValue progress = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.progress)
        .first
        ?.progress;
    expect(progress, isNotNull);
    expect(progress?.value, 0.3);

    brightness.dispose();
  });

  test('Set auto Brightness', () async {
    when(control.watchAutoBrightness()).thenAnswer((_) => Future.value(false));
    when(control.watchCurrentBrightness()).thenAnswer((_) => Future.value(0.5));

    // Should receive brightness spec.
    final brightness = Brightness(control);
    Spec spec = await brightness.getSpec();

    // Now set brightness to auto.
    brightness.update(Value.withButton(ButtonValue(
      label: null,
      action: Brightness.autoAction,
    )));

    verify(control.setAutoBrightness());

    // Should follow immediately by brightness spec.
    spec = await brightness.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Should be MISSING a button to set auto brightness.
    final hasButton =
        spec.groups.first.values.any((v) => v.$tag == ValueTag.button);
    expect(hasButton, isFalse);

    brightness.dispose();
  });
}

class MockControl extends Mock implements ControlProxy {}

class MockProxyController extends Mock
    implements AsyncProxyController<Control> {}
