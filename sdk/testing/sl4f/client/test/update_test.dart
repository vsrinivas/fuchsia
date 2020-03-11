// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

import 'package:sl4f/sl4f.dart';

class MockSl4f extends Mock implements Sl4f {}

void main(List<String> args) {
  MockSl4f sl4f;
  Update update;

  setUp(() {
    sl4f = MockSl4f();
    update = Update(sl4f);
  });

  group(Update, () {
    test('CheckNow', () async {
      when(sl4f.request('update_facade.CheckNow', {'service-initiated': false}))
          .thenAnswer((_) async => {'check_started': 'Started'});

      expect(await update.checkNow(), CheckStartedResult.started);
    });

    test('CheckNow service initiated', () async {
      when(sl4f.request('update_facade.CheckNow', {'service-initiated': true}))
          .thenAnswer((_) async => {'check_started': 'AlreadyInProgress'});

      expect(await update.checkNow(serviceInitiated: true),
          CheckStartedResult.alreadyInProgress);
    });

    test('GetCurrentChannel', () async {
      when(sl4f.request('update_facade.GetCurrentChannel'))
          .thenAnswer((_) async => 'current-channel');

      expect(await update.getCurrentChannel(), 'current-channel');
    });

    test('GetTargetChannel', () async {
      when(sl4f.request('update_facade.GetTargetChannel'))
          .thenAnswer((_) async => 'target-channel');

      expect(await update.getTargetChannel(), 'target-channel');
    });

    test('SetTargetChannel', () async {
      await update.setTargetChannel('target-channel');
      verify(sl4f.request(
              'update_facade.SetTargetChannel', {'channel': 'target-channel'}))
          .called(1);
    });

    test('GetChannelList', () async {
      when(sl4f.request('update_facade.GetChannelList'))
          .thenAnswer((_) async => ['channel1', 'channel1']);

      expect(await update.getChannelList(), ['channel1', 'channel1']);
    });
  });
}
