// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';
import 'dart:math';

import 'package:image/image.dart';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:flutter_driver_sl4f/flutter_driver_sl4f.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';
import 'package:webdriver/sync_io.dart' show WebDriver;

const _chromeDriverPath = 'runtime_deps/chromedriver';
const ermineUrl = 'fuchsia-pkg://fuchsia.com/ermine#meta/ermine.cmx';
const simpleBrowserUrl =
    'fuchsia-pkg://fuchsia.com/simple-browser#meta/simple-browser.cmx';

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
  final _browserDrivers = <FlutterDriver>[];
  final _browserConnectors = <FlutterDriverConnector>[];
  final WebDriverConnector _webDriverConnector;

  /// Constructor.
  ErmineDriver(this.sl4f)
      : _connector = FlutterDriverConnector(sl4f),
        _component = Component(sl4f),
        _webDriverConnector = WebDriverConnector(_chromeDriverPath, sl4f);

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
    // TODO(http://fxbug.dev/60644): Uncomment once fixed.
    // final result = await sl4f.ssh.run('session_control restart');
    // if (result.exitCode != 0) {
    //   fail('failed to restart workstation session.');
    // }

    // Wait for Ermine to start.
    await isRunning(ermineUrl);

    // Initialize Ermine's flutter driver and web driver connectors.
    await _connector.initialize();
    await _webDriverConnector.initialize();

    // Now connect to ermine.
    _driver = await _connector.driverForIsolate('ermine');
    if (_driver == null) {
      fail('unable to connect to ermine.');
    }

    // Close any pre-existing views.
    await _driver.requestData('closeAll');
  }

  /// Closes [FlutterDriverConnector] and performs cleanup.
  Future<void> tearDown() async {
    await _driver?.close();
    await _connector.tearDown();

    for (final browserDriver in _browserDrivers) {
      await browserDriver?.close();
    }

    for (final browserConnector in _browserConnectors) {
      await browserConnector?.tearDown();
    }

    await _webDriverConnector?.tearDown();
  }

  /// Launch a component given its [componentUrl].
  Future<void> launch(String componentUrl,
      {Duration timeout = const Duration(seconds: 30)}) async {
    final result = await sl4f.ssh.run('session_control add $componentUrl');
    if (result.exitCode != 0) {
      fail('failed to launch component: $componentUrl.');
    }
    if (!await isRunning(componentUrl, timeout: timeout)) {
      fail('Timed out waiting to launch $componentUrl');
    }
  }

  /// Returns true if a component is running.
  Future<bool> isRunning(String componentUrl,
      {Duration timeout = const Duration(seconds: 30)}) async {
    final end = DateTime.now().add(timeout);
    while (DateTime.now().isBefore(end)) {
      var components = await component.list();
      if (components.where((e) => e.contains(componentUrl)).isNotEmpty) {
        return true;
      }
    }
    return false;
  }

  /// Returns true if a component is stopped.
  Future<bool> isStopped(String componentUrl,
      {Duration timeout = const Duration(seconds: 30)}) async {
    final end = DateTime.now().add(timeout);
    while (DateTime.now().isBefore(end)) {
      var components = await component.list();
      if (components.where((e) => e.contains(componentUrl)).isEmpty) {
        return true;
      }
    }
    return false;
  }

  /// Got to the Overview screen.
  Future<void> gotoOverview() async {
    await _driver.requestData('overview');
    await _driver.waitFor(find.byValueKey('overview'));
  }

  /// Launches a simple browser and returns a [FlutterDriver] connected to it.
  ///
  /// Opens another new tab as soon as the browser is launched, unless you set
  /// [openNewTab] to false. Likewise, set [fullscreen] to false if you do not
  /// want the browser to expand its size to full-screen upon its launch.
  Future<FlutterDriver> launchAndWaitForSimpleBrowser({
    bool openNewTab = true,
    bool fullscreen = true,
  }) async {
    await launch(simpleBrowserUrl);
    final runningComponents = await component.list();
    expect(
        runningComponents.where((c) => c.contains(simpleBrowserUrl)).length, 1);

    // Initializes the browser's flutter driver connector.
    final browserConnector = FlutterDriverConnector(sl4f);
    _browserConnectors.add(browserConnector);
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
    _browserDrivers.add(browserDriver);
    if (browserDriver == null) {
      fail('unable to connect to simple browser.');
    }

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
    }

    return browserDriver;
  }

  /// Returns a web driver connected to a given URL.
  Future<WebDriver> getWebDriverFor(String hostUrl) async {
    return (await _webDriverConnector.webDriversForHost(hostUrl)).single;
  }

  Future<Rectangle> getViewRect(String viewUrl,
      [Duration timeout = const Duration(seconds: 30)]) async {
    final component = await waitForView(viewUrl, timeout);
    final viewport = component['viewport'];
    final viewRect =
        viewport.split(',').map((e) => double.parse(e).round()).toList();

    return Rectangle(viewRect[0], viewRect[1], viewRect[2], viewRect[3]);
  }

  /// Finds the first launched component given its [viewUrl] and returns it's
  /// Inspect data. Waits for [timeout] duration for view to launch.
  Future<Map<String, dynamic>> waitForView(String viewUrl,
      [Duration timeout = const Duration(seconds: 30)]) async {
    final end = DateTime.now().add(timeout);
    while (DateTime.now().isBefore(end)) {
      final views = await launchedViews();
      final view = views.firstWhere((view) => view['url'] == viewUrl,
          orElse: () => null);
      if (view != null) {
        return view;
      }
      // Wait a second to query inspect again.
      await Future.delayed(Duration(seconds: 1));
    }
    fail('Failed to find component: $viewUrl');
    // ignore: dead_code
    return null;
  }

  /// Returns [Inspect] data for all launched views.
  Future<List<Map<String, dynamic>>> launchedViews() async {
    final views = <Map<String, dynamic>>[];
    final snapshot = await Inspect(sl4f).snapshotRoot('ermine.cmx');
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
      {Duration timeout = const Duration(seconds: 30)}) async {
    final completer = Completer<T>();
    final end = DateTime.now().add(timeout);
    while (DateTime.now().isBefore(end)) {
      final result = await completion();
      if (result == null || result is bool && result == false) {
        continue;
      }
      completer.complete(result);
      break;
    }
    return completer.future;
  }
}
