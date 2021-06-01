// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

// ignore_for_file: implementation_imports
import 'package:ermine/src/widgets/oobe/channel.dart';
import 'package:fidl_fuchsia_update_channelcontrol/fidl_async.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

const List<String> channels = [
  'channelA',
  'channelB',
];

void main() {
  MockControl control;

  setUp(() async {
    control = MockControl();
  });

  test('ChannelModel should be able to get the default channel.', () async {
    when(control.getTarget()).thenAnswer((_) => Future.value(channels[0]));
    when(control.getTargetList()).thenAnswer((_) => Future.value(channels));

    final model = ChannelModel(control: control);

    List<String> modelChannels = await model.channels;

    expect(model.channel.value, channels[0]);
    expect(modelChannels, channels);
  });

  test('ChannelModel should be able to change the target channel.', () async {
    when(control.getTarget()).thenAnswer((_) => Future.value(channels[0]));
    when(control.getTargetList()).thenAnswer((_) => Future.value(channels));

    final model = ChannelModel(control: control);

    await model.channels;

    // Get initial channel
    expect(model.channel.value, channels[0]);

    when(control.getTarget()).thenAnswer((_) => Future.value(channels[1]));

    // Change the channel
    await model.setCurrentChannel(channels[1]);

    expect(model.channel.value, channels[1]);
  });
}

class MockControl extends Mock implements ChannelControlProxy {}
