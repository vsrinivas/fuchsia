// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:fidl_fuchsia_update_channelcontrol/fidl_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';
import 'package:settings/settings.dart';

void main() {
  MockControl control;

  setUp(() async {
    control = MockControl();
  });

  test('Default Channel Spec', () async {
    when(control.getTarget()).thenAnswer((_) => Future.value('devhost'));

    final channel = Channel(control);
    Spec spec = await channel.getSpec();

    expect(spec.title, 'Channel');
    expect(spec.groups.first.values.first.text.text, 'devhost');
  });
}

class MockControl extends Mock implements ChannelControlProxy {}
