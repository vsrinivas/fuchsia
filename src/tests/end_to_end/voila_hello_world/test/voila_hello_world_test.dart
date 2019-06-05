// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:image/image.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

/// Returns true if [image] has the right shade of green at its central pixel.
bool _isCenterGreen(Image image) {
  var pixelIntValue = image.getPixel(image.width ~/ 2, image.height ~/ 2);
  return getRed(pixelIntValue) == 0 &&
      getGreen(pixelIntValue) == 255 &&
      getBlue(pixelIntValue) == 65;
}

const _delay = Duration(seconds: 2);

const _runVoilaCommand = [
  'tiles_ctl',
  'add',
  'fuchsia-pkg://fuchsia.com/voila#meta/voila.cmx',
  '--count_of_replicas=1',
  '--session_shell=fuchsia-pkg://fuchsia.com/voila_tests#meta/session_shell.cmx',
];

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Scenic scenicDriver;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    scenicDriver = sl4f.Scenic(sl4fDriver);
  });

  tearDown(() async {
    await sl4fDriver.ssh('tiles_ctl quit');
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  test('test shell is displayed', () async {
    await sl4fDriver.ssh('tiles_ctl start');
    await Future.delayed(_delay);
    await sl4fDriver.ssh(_runVoilaCommand.join(' '));
    await Future.delayed(_delay);
    final screen = await scenicDriver.takeScreenshot(dumpName: 'screen');
    if (!_isCenterGreen(screen)) {
      fail('The center pixel is NOT the right shade of green');
    }
    print('The center pixel is the right shade of green.');
  },
      // This is a large test that waits for the DUT to come up and to start
      // rendering something.
      timeout: Timeout.none);
}
