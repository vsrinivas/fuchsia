// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:mockito/mockito.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

class MockSl4f extends Mock implements Sl4f {}

void main(List<String> args) {
  MockSl4f sl4f;
  VirtualCamera virtualCamera;

  setUp(() {
    sl4f = MockSl4f();
    virtualCamera = VirtualCamera(sl4f);
  });

  group(VirtualCamera, () {
    test('AddStreamConfig', () async {
      const index = 0;
      const width = 200;
      const height = 400;

      await virtualCamera.addStreamConfig(index, width, height);
      verify(sl4f.request('virtual_camera_facade.AddStreamConfig', {
        'index': index,
        'width': width,
        'height': height,
      })).called(1);
    });

    test('AddToDeviceWatcher', () async {
      await virtualCamera.addToDeviceWatcher();
      verify(sl4f.request('virtual_camera_facade.AddToDeviceWatcher', {}))
          .called(1);
    });
  });
}
