// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:image/image.dart';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:flutter_driver_sl4f/flutter_driver_sl4f.dart';
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';
import 'package:webdriver/sync_io.dart' show WebDriver;

const _chromeDriverPath = 'runtime_deps/chromedriver';

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
    final result = await sl4f.ssh.run('session_control restart');
    if (result.exitCode != 0) {
      fail('failed to restart workstation session.');
    }

    // Wait for Ermine to start.
    expect(await component.search('ermine.cmx'), isTrue);

    // Initialize Ermine's flutter driver and web driver connectors.
    await _connector.initialize();
    await _webDriverConnector.initialize();

    // Now connect to ermine.
    _driver = await _connector.driverForIsolate('ermine');
    if (_driver == null) {
      fail('unable to connect to ermine.');
    }
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
  Future<void> launch(String componentUrl) async {
    final result = await sl4f.ssh.run('session_control add $componentUrl');
    if (result.exitCode != 0) {
      fail('failed to launch component: $componentUrl.');
    }
  }

  /// Got to the Overview screen.
  Future<void> gotoOverview() async {
    await _driver.requestData('overview');
    await _driver.waitFor(find.byValueKey('overview'));
  }

  /// Launches a simple browser and returns a [FlutterDriver] connected to it.
  Future<FlutterDriver> launchAndWaitForSimpleBrowser() async {
    await launch(
        'fuchsia-pkg://fuchsia.com/simple-browser#meta/simple-browser.cmx');

    // Initilize the browser's flutter driver connector.
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

    // Connect to the browser.
    // TODO(fxb/66577): Get the driver of the last isolate once it's supported by
    // [FlutterDriverConnector] in flutter_driver_sl4f.dart
    final browserDriver =
        await browserConnector.driverForIsolate('simple-browser');
    _browserDrivers.add(browserDriver);
    if (browserDriver == null) {
      fail('unable to connect to simple browser.');
    }

    await browserDriver.waitUntilNoTransientCallbacks();

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
  /// [Color] is encoded as  0xAABBGGRR.
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
}
