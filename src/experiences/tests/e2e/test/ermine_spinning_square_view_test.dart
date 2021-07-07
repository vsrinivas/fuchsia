// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:ermine_driver/ermine_driver.dart';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:image/image.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

/// Tests that the DUT running ermine can do the following:
///  - Launch `spinning_square_view` ephemeral package.
///  - Verify it is show by taking its screenshot.
void main() {
  late Sl4f sl4f;
  late ErmineDriver ermine;

  setUpAll(() async {
    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();

    ermine = ErmineDriver(sl4f);
    await ermine.setUp();
  });

  tearDownAll(() async {
    // Any of these may end up being null if the test fails in setup.
    await ermine.tearDown();
    await sl4f.stopServer();
    sl4f.close();
  });

  // Take a screenshot until it's non-black or timeout.
  Future<Image?> screenshotUntilNotBlack(Rectangle rect,
      {Duration timeout = const Duration(seconds: 30)}) async {
    final end = DateTime.now().add(timeout);
    while (DateTime.now().isBefore(end)) {
      final image = await ermine.screenshot(rect);
      bool isAllBlack = image.data.every((pixel) => pixel & 0x00ffffff == 0);
      if (!isAllBlack) {
        return image;
      }
    }
    return null;
  }

  test('Verify spinning square view is shown', () async {
    const componentUrl =
        'fuchsia-pkg://fuchsia.com/spinning_square_view#meta/spinning_square_view.cmx';
    await ermine.launch(componentUrl);
    expect(
        await ermine.waitFor(() async {
          var views = (await ermine.launchedViews())
              .where((view) => view['url'] == componentUrl)
              .toList();
          return views.length == 1;
        }),
        isTrue);

    // Get the view rect.
    final viewRect = await ermine.getViewRect(componentUrl);
    // Give the view couple of seconds to draw before taking its screenshot.
    await Future.delayed(Duration(seconds: 2));
    final screenshot = await screenshotUntilNotBlack(viewRect);
    expect(screenshot, isNotNull);
    final histogram = ermine.histogram(screenshot!);

    // spinning_square_view displays a red square on purple background.
    const purple = 0xffb73a67; //  (0xAABBGGRR)
    const red = 0xff5700f5; //  (0xAABBGGRR)
    // We should find atleast 2 colors (the visible cursor adds its own color).
    expect(histogram.keys.length >= 2, isTrue);
    expect(histogram[purple], isNotNull);
    expect(histogram[red], isNotNull);
    expect(histogram[purple]! > histogram[red]!, isTrue);

    // Close the view.
    await ermine.driver.requestData('close');
    // Verify the view is closed.
    await ermine.driver.waitForAbsent(find.text('spinning_square_view'));
    // Ensure the component has stopped running.
    await ermine.isStopped(componentUrl);
  });
}
