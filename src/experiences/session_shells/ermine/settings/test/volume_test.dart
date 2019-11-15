// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fuchsia_media/fidl_async.dart' as vol;
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';
import 'package:settings/settings.dart';

void main() {
  test('Volume', () async {
    final control = MockControl();
    when(control.systemGainMuteChanged)
        .thenAnswer((response) => _buildStats(-9, false));

    Volume volume = Volume(control);

    // Should receive volume spec.
    Spec spec = await volume.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm progress value is correct
    ProgressValue progress = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.progress)
        .first
        ?.progress;
    expect(progress, isNotNull);
    expect(progress?.value, 0.8);

    // Confirm text displayed is correct
    TextValue text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(text?.text, '80');

    // Confirm min & max buttons are present
    Iterable hasButtons =
        spec.groups.first.values.where((v) => v.$tag == ValueTag.button);
    expect(hasButtons.length, 2);

    volume.dispose();
  });

  test('Change Volume', () async {
    final control = MockControl();
    when(control.systemGainMuteChanged)
        .thenAnswer((_) => _buildStats(-9, false));

    Volume volume = Volume(control);

    // Should receive volume spec.
    Spec spec = await volume.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm progress value is correct
    ProgressValue progress = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.progress)
        .first
        ?.progress;
    expect(progress, isNotNull);
    expect(progress?.value, 0.8);

    // Confirm text displayed is correct
    TextValue text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(text?.text, '80');

    // Change volume level
    volume.model.volume = .9;

    // Should receive volume spec.
    spec = await volume.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    progress = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.progress)
        .first
        ?.progress;
    expect(progress, isNotNull);
    expect(progress?.value, 0.9);

    // Confirm text displayed is correct
    text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(text?.text, '90');

    // Confirm min & max buttons are present
    Iterable hasButtons =
        spec.groups.first.values.where((v) => v.$tag == ValueTag.button);
    expect(hasButtons.length, 2);

    volume.dispose();
  });

  test('Max Volume', () async {
    final control = MockControl();
    when(control.systemGainMuteChanged)
        .thenAnswer((_) => _buildStats(0, false));

    Volume volume = Volume(control);

    // Should receive volume spec.
    Spec spec = await volume.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm progress value is correct
    ProgressValue progress = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.progress)
        .first
        ?.progress;
    expect(progress, isNotNull);
    expect(progress?.value, 1);

    // Confirm text displayed is correct
    TextValue text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(text?.text, '100');

    // Change volume level above max accepted dB
    volume.model.volume = volume.model.gainToLevel(10);

    // Should receive volume spec.
    spec = await volume.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm volume is still at max
    progress = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.progress)
        .first
        ?.progress;
    expect(progress, isNotNull);
    expect(progress?.value, 1);

    // Confirm text displayed is correct
    text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(text?.text, '100');

    // Confirm min & max buttons are present
    Iterable hasButtons =
        spec.groups.first.values.where((v) => v.$tag == ValueTag.button);
    expect(hasButtons.length, 2);

    volume.dispose();
  });

  test('Min Volume', () async {
    final control = MockControl();
    when(control.systemGainMuteChanged)
        .thenAnswer((_) => _buildStats(-45, false));

    Volume volume = Volume(control);

    // Should receive volume spec.
    Spec spec = await volume.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm progress value is correct
    ProgressValue progress = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.progress)
        .first
        ?.progress;
    expect(progress, isNotNull);
    expect(progress?.value, 0);

    // Confirm text displayed is correct
    TextValue text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(text?.text, '0');

    // Change volume level below min accepted dB
    volume.model.volume = volume.model.gainToLevel(-55);

    // Should receive volume spec.
    spec = await volume.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm volume is still at min
    progress = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.progress)
        .first
        ?.progress;
    expect(progress, isNotNull);
    expect(progress?.value, 0);

    // Confirm text displayed is correct
    text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(text?.text, '0');

    // Confirm min & max buttons are present
    Iterable hasButtons =
        spec.groups.first.values.where((v) => v.$tag == ValueTag.button);
    expect(hasButtons.length, 2);

    volume.dispose();
  });
}

Stream<vol.AudioCore$SystemGainMuteChanged$Response> _buildStats(
    double gainDB, bool muted) {
  return Stream.value(
      vol.AudioCore$SystemGainMuteChanged$Response(gainDB, muted));
}

class MockControl extends Mock implements vol.AudioCoreProxy {}

class MockReponse extends Mock implements vol.AudioCore {}
