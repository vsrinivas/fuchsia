// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:fidl_fuchsia_update_channelcontrol/fidl_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';
import 'package:settings/settings.dart';

const List<String> channels = [
  'channelA',
  'channelB',
];

void main() {
  late MockControl control;

  setUp(() async {
    control = MockControl();
  });

  test('Default Channel Spec', () async {
    when(control.getTarget()).thenAnswer((_) => Future.value(channels[0]));
    when(control.getTargetList()).thenAnswer((_) => Future.value(channels));

    final channel = Channel(control);
    Spec spec = await channel.getSpec();

    expect(spec.title, 'Channel');
    expect(spec.groups?.first.values?.first.text?.text, channels[0]);
  });

  test('Change Channel', () async {
    when(control.getTarget()).thenAnswer((_) => Future.value(channels[0]));
    when(control.getTargetList()).thenAnswer((_) => Future.value(channels));

    final channel = Channel(control);
    Spec specA = await channel.getSpec();

    expect(specA.title, 'Channel');
    expect(specA.groups?.first.values?.first.text?.text, channels[0]);

    // Change channel
    channel.model.channel = channels[1];

    // Wait one event cycle for the change
    await channel.getSpec();
    Spec specB = await channel.getSpec();
    expect(specB.groups?.first.values?.first.text?.text, channels[1]);
  });
}

class MockControl extends Mock implements ChannelControlProxy {}
