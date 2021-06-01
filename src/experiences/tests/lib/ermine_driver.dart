// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';
import 'dart:math';

import 'package:fidl_fuchsia_input/fidl_async.dart';
import 'package:fidl_fuchsia_ui_input3/fidl_async.dart' hide KeyEvent;
import 'package:flutter_driver/flutter_driver.dart';
import 'package:flutter_driver_sl4f/flutter_driver_sl4f.dart';
import 'package:image/image.dart' hide Point;
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

const ermineUrl = 'fuchsia-pkg://fuchsia.com/ermine#meta/ermine.cmx';
const simpleBrowserUrl =
    'fuchsia-pkg://fuchsia.com/simple-browser#meta/simple-browser.cmx';
const terminalUrl = 'fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cmx';

// USB HID code for ENTER key.
// See <https://www.usb.org/sites/default/files/documents/hut1_12v2.pdf>
const kEnterKey = 40;
const kBackspaceKey = 0x2a;

const waitForTimeout = Duration(seconds: 30);

/// Defines a completion function that can be waited on with a timout.
typedef WaitForCompletion<T> = Future<T> Function();

/// Defines a test utility class to drive Ermine during integration test using
/// Flutter Driver. This utility will grow with more convenience methods in the
/// future useful for testing.
class ErmineDriver {
  /// The instance of [Sl4f] used to connect to Ermine flutter app.
  final Sl4f sl4f;
  final Component _component;

  FlutterDriver _driver;
  final FlutterDriverConnector _connector;

  /// Constructor.
  ErmineDriver(this.sl4f)
      : _connector = FlutterDriverConnector(sl4f),
        _component = Component(sl4f);

  /// The instance of [FlutterDriver] that is connected to Ermine flutter app.
  FlutterDriver get driver => _driver;

  /// The instance of [Component] that is connected to the DUT.
  Component get component => _component;

  /// Set up the test environment for Ermine.
  ///
  /// This restarts the workstation session and connects to the running instance
  /// of Ermine using FlutterDriver.
  Future<void> setUp() async {
    // Restart the workstation session.
    final result = await sl4f.ssh.run('session_control restart');
    if (result.exitCode != 0) {
      fail('failed to restart workstation session.');
    }

    // Wait for Ermine to start.
    await isRunning(ermineUrl);

    // Initialize Ermine's flutter driver and web driver connectors.
    await _connector.initialize();

    // Now connect to ermine.
    _driver = await _connector.driverForIsolate('ermine');
    if (_driver == null) {
      fail('unable to connect to ermine.');
    }

    // Close any pre-existing views.
    await _driver.requestData('closeAll');
    expect((await launchedViews()).isEmpty, isTrue);
  }

  /// Closes [FlutterDriverConnector] and performs cleanup.
  Future<void> tearDown() async {
    await _driver?.close();
    await _connector.tearDown();
  }

  /// Launch a component given its [componentUrl].
  Future<bool> launch(String componentUrl,
      {Duration timeout = waitForTimeout}) async {
    final result = await sl4f.ssh.run('session_control add $componentUrl');
    if (result.exitCode != 0) {
      fail('failed to launch component: $componentUrl.');
    }
    final running = await isRunning(componentUrl, timeout: timeout);
    if (!running) {
      fail('Timed out waiting to launch $componentUrl');
    }

    return running;
  }

  /// Returns true if a component is running.
  Future<bool> isRunning(String componentUrl,
      {Duration timeout = waitForTimeout}) async {
    return waitFor(() async {
      return (await component.list())
          .where((e) => e.contains(componentUrl))
          .isNotEmpty;
    }, timeout: timeout);
  }

  /// Returns true if a component is stopped.
  Future<bool> isStopped(String componentUrl,
      {Duration timeout = waitForTimeout}) async {
    return waitFor(() async {
      return (await component.list())
          .where((e) => e.contains(componentUrl))
          .isEmpty;
    }, timeout: timeout);
  }

  /// Got to the Overview screen.
  Future<void> gotoOverview() async {
    await _driver.requestData('overview');
    await _driver.waitUntilNoTransientCallbacks(timeout: Duration(seconds: 2));
    await _driver.waitFor(find.byValueKey('overview'));
  }

  /// Enters text into Ask bar.
  /// Optionally clear existing content and goto Ask in Overview.
  Future<void> enterTextInAsk(
    String text, {
    bool clear = true,
    bool gotoOverview = false,
  }) async {
    final input = Input(sl4f);

    if (gotoOverview) {
      await this.gotoOverview();
    } else {
      // Invoke Ask using keyboard shortcut.
      await twoKeyShortcut(Key.leftAlt, Key.space);
      await driver.waitFor(find.byType('Ask'));
    }

    if (clear) {
      await driver.requestData('clear');
      await driver.waitUntilNoTransientCallbacks();
      // Add a space and delete using backspace to resolve auto-complete.
      await input.text(' ');
      await input.keyPress(kBackspaceKey);
      await driver.waitUntilNoTransientCallbacks();
    }

    await input.text(text);

    // Verify text was injected into flutter widgets.
    await driver.waitUntilNoTransientCallbacks();
    await driver.waitFor(find.text(text));
    final askResult = await driver.getText(find.descendant(
      of: find.byType('AskTextField'),
      matching: find.text(text),
    ));
    expect(askResult, text);
  }

  /// Tap the location given by [offset] in screen co-ordinates.
  ///
  /// Normalize to screen size of 1000x1000 expected by [Input.tap].
  Future<void> tap(DriverOffset offset, {bool normalize = true}) async {
    var point = Point<int>(offset.dx.toInt(), offset.dy.toInt());
    if (normalize) {
      // Get the size of screen by getting the size of the App widget.
      final screen = await driver.getBottomRight(find.byType('App'));
      point = Point<int>(
        (offset.dx * 1000) ~/ screen.dx,
        (offset.dy * 1000) ~/ screen.dy,
      );
    }

    final input = Input(sl4f);
    return input.tap(point);
  }

  /// Invoke a two key keyboard shortcut.
  Future<void> twoKeyShortcut(Key modifier, Key key) async {
    const key1Press = Duration(milliseconds: 100);
    const key2Press = Duration(milliseconds: 200);
    const key2Release = Duration(milliseconds: 400);
    const key1Release = Duration(milliseconds: 600);

    final input = Input(sl4f);
    await input.keyEvents([
      KeyEvent(modifier, key1Press, KeyEventType.pressed),
      KeyEvent(key, key2Press, KeyEventType.pressed),
      KeyEvent(key, key2Release, KeyEventType.released),
      KeyEvent(modifier, key1Release, KeyEventType.released),
    ]);
    await driver.waitUntilNoTransientCallbacks();
  }

  /// Launches a simple browser and returns a [FlutterDriver] connected to it.
  Future<FlutterDriver> launchSimpleBrowser() async {
    expect(await launch(simpleBrowserUrl), isTrue);

    // Initializes the browser's flutter driver connector.
    final browserConnector = FlutterDriverConnector(sl4f);
    await browserConnector.initialize();

    // Checks if Simple Browser is running.
    // TODO(fxb/66577): Get the last isolate once it's supported by
    // [FlutterDriverConnector] in flutter_driver_sl4f.dart
    final browserIsolate = await browserConnector.isolate('simple-browser');
    if (browserIsolate == null) {
      fail('couldn\'t find simple browser.');
    }

    // Connects to the browser.
    // TODO(fxb/66577): Get the driver of the last isolate once it's supported by
    // [FlutterDriverConnector] in flutter_driver_sl4f.dart
    final browserDriver =
        await browserConnector.driverForIsolate('simple-browser');
    if (browserDriver == null) {
      fail('unable to connect to simple browser.');
    }

    return browserDriver;
  }

  /// Launches a simple browser and sets up options for test convenience.
  ///
  /// Opens another new tab as soon as the browser is launched, unless you set
  /// [openNewTab] to false. Contrarily, set [fullscreen] to true if you want
  /// the browser to expand its size to full-screen upon its launch.
  /// Also, you can set the text entry emulation of the browser's flutter driver
  /// using [enableTextEntryEmulation], which has false by default.
  Future<FlutterDriver> launchAndWaitForSimpleBrowser({
    bool openNewTab = true,
    bool fullscreen = false,
    bool enableTextEntryEmulation = false,
  }) async {
    final browserDriver = await launchSimpleBrowser();

    // Set the flutter driver's text entry emulation.
    await browserDriver.setTextEntryEmulation(
        enabled: enableTextEntryEmulation);
    await browserDriver.waitUntilNoTransientCallbacks();

    // Expands the simple browser to be a full-sized screen, if required.
    if (fullscreen) {
      await _driver.requestData('fullscreen');
    }

    // Opens another tab other than the tab opened on browser's launch,
    // if required.
    if (openNewTab) {
      final addTab = find.byValueKey('new_tab');
      await browserDriver.waitFor(addTab);

      await browserDriver.tap(addTab);
      await browserDriver.waitFor(find.text('NEW TAB'),
          timeout: Duration(seconds: 10));
      print('Opened a new tab');
    }

    return browserDriver;
  }

  Future<Rectangle> getViewRect(String viewUrl,
      [Duration timeout = waitForTimeout]) async {
    final component = await waitForView(viewUrl, timeout);
    final viewport = component['viewport'];
    final viewRect =
        viewport.split(',').map((e) => double.parse(e).round()).toList();

    return Rectangle(viewRect[0], viewRect[1], viewRect[2], viewRect[3]);
  }

  /// Finds the first launched component given its [viewUrl] and returns it's
  /// Inspect data. Waits for [timeout] duration for view to launch.
  Future<Map<String, dynamic>> waitForView(String viewUrl,
      [Duration timeout = waitForTimeout]) async {
    return waitFor(() async {
      await _driver.waitUntilNoTransientCallbacks(
          timeout: Duration(seconds: 2));
      final views = await launchedViews();
      final view = views.firstWhere((view) => view['url'] == viewUrl,
          orElse: () => null);
      return view;
    }, timeout: timeout);
  }

  /// Returns [Inspect] data for all launched views.
  Future<List<Map<String, dynamic>>> launchedViews() async {
    final views = <Map<String, dynamic>>[];
    final snapshot = await Inspect(sl4f).snapshotRoot('ermine.cmx');
    if (snapshot == null) {
      return views;
    }
    final workspace = snapshot['workspaces'];
    if (workspace == null) {
      return views;
    }

    final clusters = <Map<String, dynamic>>[];
    int instance = 0;
    while (workspace['cluster-$instance'] != null) {
      clusters.add(workspace['cluster-${instance++}']);
    }

    for (final cluster in clusters) {
      int instance = 0;
      while (cluster['component-$instance'] != null) {
        views.add(cluster['component-${instance++}']);
      }
    }
    return views;
  }

  /// Take a screenshot of a View given its screen co-ordinates.
  Future<Image> screenshot(Rectangle rect) async {
    final scenic = Scenic(sl4f);
    final image = await scenic.takeScreenshot();
    return copyCrop(image, rect.left, rect.top, rect.width, rect.height);
  }

  /// Returns a histogram, i.e. occurences of colors, in an image.
  /// [Color] is encoded as 0xAABBGGRR.
  Map<int, int> histogram(Image image) {
    final colors = <int, int>{};
    for (int j = 0; j < image.height; j++) {
      for (int i = 0; i < image.width; i++) {
        final color = image.getPixel(i, j);
        colors[color] = (colors[color] ?? 0) + 1;
      }
    }
    return colors;
  }

  /// Returns the difference rate between two same-sized images.
  /// The range is from 0 to 1, and the closer to 0 the rate is, the more
  /// identical the two images.
  double screenshotsDiff(Image a, Image b) {
    expect(
      a.data.length,
      b.data.length,
      reason: 'The resolution of two images are different',
    );

    var diff = 0;
    for (var i = 0; i < a.data.length; i++) {
      if (a.data[i] != b.data[i]) {
        diff++;
      }
    }
    final diffRate = (diff / a.data.length);
    return diffRate;
  }

  /// Saves a screenshot of a View as a png image file.
  ///
  /// Mainly used to create initial golden images for image diff tests.
  /// To do this, call it in your `test()` before writing your image-diff test.
  /// For example,
  /// ```
  /// test('Image diff test' () async {
  ///   ErmineDriver ermine = ErmineDriver(sl4f);
  ///   await ermine.launch(componentUrl);
  ///   final viewRect = await ermine.getViewRect(componentUrl);
  ///   final screenshot = await ermine.screenshot(viewRect);
  ///
  ///   ermine.saveImageAs(screenshot, 'screenshot.png');
  /// });
  /// ```
  /// You will be able to find the output files under //out/default/ on your host
  /// machine once you run the test successfully. If they look as you want,
  /// move them under //src/experiences/tests/e2e/test/scuba_goldens so that
  /// you can use them as your golden images. Once you have them there and in
  /// BUILD, you are good to write your image diff test using [screenshot] and
  /// [goldenDiff] and remove this method call.
  ///
  /// Note that due to the size limit of the data used for the communication
  /// between sl4f and the host, the screenshots over 4MB in size will be
  /// cropped out to 1536x864 (fxb/70233).
  void saveImageAs(Image image, String file) async {
    final fileName = _sanitizeGoldenFileName(file);
    File(fileName).writeAsBytesSync(encodePng(image));
  }

  /// Returns the difference rate between an image and the correspondant golden
  /// image stored in the host. The range is from 0 to 1, and the closer to 0
  /// the rate is, the more identical the two images.
  double goldenDiff(Image image, String golden) {
    final goldenFileName = _sanitizeGoldenFileName(golden);
    final goldenFilePath = 'dartlang/scuba_goldens/$goldenFileName';
    final goldenFile = File(goldenFilePath);
    expect(goldenFile.existsSync(), isTrue,
        reason: 'No such file or directory: $goldenFilePath');

    final goldenImage = decodePng(goldenFile.readAsBytesSync());
    if (image.length != golden.length) {
      final resizedImage = copyResize(
        image,
        width: goldenImage.width,
        height: goldenImage.height,
      );
      return screenshotsDiff(resizedImage, goldenImage);
    }
    return screenshotsDiff(image, goldenImage);
  }

  String _sanitizeGoldenFileName(String file) {
    if (file.contains('.')) {
      final splits = file.split('.');
      expect(splits.length, 2,
          reason: 'The golden file name can contain only one dot(.) '
              'for its extension.');
      final fileType = splits.last;
      expect(fileType.toLowerCase(), 'png',
          reason: 'The file type should be png');
      return file;
    } else {
      return '$file.png';
    }
  }

  /// A helper function to wait for completion of a computation within timeout.
  Future<T> waitFor<T>(WaitForCompletion<T> completion,
      {Duration timeout = waitForTimeout}) async {
    final end = DateTime.now().add(timeout);
    T result;
    while (DateTime.now().isBefore(end)) {
      result = await completion();
      if (result == null || result is bool && result == false) {
        // Add a delay so as not to spam the system.
        await Future.delayed(Duration(seconds: 1));
        continue;
      }
      break;
    }
    return result;
  }
}
