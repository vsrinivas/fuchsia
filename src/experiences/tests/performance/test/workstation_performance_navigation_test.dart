// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: import_of_legacy_library_into_null_safe

import 'dart:io';

import 'package:ermine_driver/ermine_driver.dart';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';

const _blueUrl = 'http://127.0.0.1:8080/blue.html';
const _catapultConverterPath = 'runtime_deps/catapult_converter';
const _greenUrl = 'http://127.0.0.1:8080/green.html';
// TODO(fxb/69334): Get rid of the space in the hint text.
const _newTabHintText = '     SEARCH';
const _pinkUrl = 'http://127.0.0.1:8080/pink.html';
const _redUrl = 'http://127.0.0.1:8080/red.html';
const _testNameTitle = 'workstation-navigation';
const _testserverUrl =
    'fuchsia-pkg://fuchsia.com/ermine_testserver#meta/ermine_testserver.cm';
const _testSuite = 'workstation-performance-navigation-test';
const _timeout = Duration(seconds: _timeoutSeconds);
const _timeoutSeconds = 10;
const _trace2jsonPath = 'runtime_deps/trace2json';
const _yellowUrl = 'http://127.0.0.1:8080/yellow.html';

// Flags to enable/disable each test in order of
// 0: Launch Simple Browser
// 1: Open 5 tabs
// 2: Rearrange tabs
// 3: Switch tabs
// 4: Close tabs
const skipTests = [true, true, true, true, true];

void main() {
  late sl4f.Dump dump;
  late Directory dumpDir;
  late ErmineDriver ermine;
  late sl4f.Input input;
  bool measureInputLatency = false;
  late sl4f.Performance performance;
  late sl4f.Sl4f sl4fDriver;
  late sl4f.WebDriverConnector webDriverConnector;

  final blueTabFinder = find.text('Blue Page');
  final greenTabFinder = find.text('Green Page');
  final newTabFinder = find.text('NEW TAB');
  final redTabFinder = find.text('Red Page');
  final pinkTabFinder = find.text('Pink Page');
  final yellowTabFinder = find.text('Yellow Page');

  late final metricsSpecs = [
    sl4f.MetricsSpec(name: 'cpu'),
    if (measureInputLatency) sl4f.MetricsSpec(name: 'input_latency'),
    sl4f.MetricsSpec(name: 'memory'),
    sl4f.MetricsSpec(name: 'scenic_frame_stats'),
  ];

  setUpAll(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    dumpDir = await Directory.systemTemp.createTemp('temp-dump');
    dump = sl4f.Dump(dumpDir.path);
    measureInputLatency = false;

    performance = sl4f.Performance(sl4fDriver, dump);
    await performance.terminateExistingTraceSession();

    ermine = ErmineDriver(sl4fDriver);
    await ermine.setUp();

    input = sl4f.Input(sl4fDriver);

    webDriverConnector =
        sl4f.WebDriverConnector('runtime_deps/chromedriver', sl4fDriver);
    await webDriverConnector.initialize();

    // Starts hosting a local http website.
    // ignore: unawaited_futures
    ermine.component.launch(_testserverUrl).catchError(print);
    expect(await ermine.isRunning(_testserverUrl), isTrue);

    expect(
        await ermine.waitFor(() async {
          const statusUrl = 'http://127.0.0.1:8080/blue.html';
          final result = await ermine.sl4f.ssh.run('curl -s $statusUrl');
          return result.stdout.isNotEmpty;
        }),
        isTrue);
    print('Started the test server');
  });

  tearDownAll(() async {
    await performance.terminateExistingTraceSession();
    dumpDir.deleteSync(recursive: true);

    // Closes the test server.
    // simple-browser is launched via [Component.launch()] since it does not
    // have a view. Therefore, it cannot be closed with ermine's flutter driver.
    // For this reason, we have to explicitly stop the http server to avoid
    // HttpException which occurs in case the test is torn down still having it
    // running.
    // TODO(fxb/69291): Remove this workaround once we can properly close hidden
    // components
    const stopUrl = 'http://127.0.0.1:8080/stop';
    final result = await ermine.sl4f.ssh.run('curl -s $stopUrl');
    expect(result.stdout, 'Stopped the server.');
    print('Stopped the test server');

    await webDriverConnector.tearDown();
    await ermine.tearDown();
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  Future<bool> _waitForTabArrangement(FlutterDriver browser,
      SerializableFinder leftTabFinder, SerializableFinder rightTabFinder,
      {Duration timeout = const Duration(seconds: 30)}) async {
    return ermine.waitFor(() async {
      final leftTabX = (await browser.getCenter(leftTabFinder)).dx;
      final rightTabX = (await browser.getCenter(rightTabFinder)).dx;
      return leftTabX < rightTabX;
    }, timeout: timeout);
  }

  test('Trace performance of launching Simple Browser', () async {
    FlutterDriver browser;

    // Initialize tracing session for performance tracking
    final traceSession = await performance.initializeTracing();
    await traceSession.start();

    // Launch simple browser
    browser = await ermine.launchSimpleBrowser();

    // Ensure simple browser's view is ready
    await ermine.driver.waitForAbsent(find.text('Launching simple-browser...'));

    // Stop tracing session
    await traceSession.stop();

    // Download and format data from trace for visualization
    final fxtTraceFile = await traceSession
        .terminateAndDownload('$_testNameTitle-simple-browser-launch-test');
    final jsonTraceFile =
        await performance.convertTraceFileToJson(_trace2jsonPath, fxtTraceFile);
    final metricsSpecSet = MetricsSpecSet(
        testName: '$_testNameTitle-simple-browser-launch-test',
        testSuite: _testSuite,
        metricsSpecs: metricsSpecs);

    expect(
        await performance.processTrace(metricsSpecSet, jsonTraceFile,
            converterPath: _catapultConverterPath),
        isNotNull);

    await browser.close();
    await ermine.driver.requestData('close');
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
  }, skip: skipTests[0]);

  test('Trace performance of opening 5 tabs in Simple Browser', () async {
    FlutterDriver browser;
    browser = await ermine.launchSimpleBrowser();

    // Enable capturing of input_latency metrics
    measureInputLatency = true;

    // Initialize tracing session for performance tracking
    final traceSession = await performance.initializeTracing();
    await traceSession.start();

    // Opens yellow.html in the first tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_yellowUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(yellowTabFinder, timeout: _timeout);

    // Opens red.html in the second tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_redUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(redTabFinder, timeout: _timeout);

    // Opens green.html in the third tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_greenUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(greenTabFinder, timeout: _timeout);

    // Opens pink.html in the fourth tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_pinkUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(pinkTabFinder, timeout: _timeout);

    // Opens blue.html in the fifth tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_blueUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(blueTabFinder, timeout: _timeout);

    // Stop tracing session
    await traceSession.stop();

    // Verify 5 tabs launched.
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(yellowTabFinder), isNotNull);
    expect(await browser.getText(redTabFinder), isNotNull);
    expect(await browser.getText(greenTabFinder), isNotNull);
    expect(await browser.getText(pinkTabFinder), isNotNull);
    expect(await browser.getText(blueTabFinder), isNotNull);
    expect(await browser.getText(find.text(_blueUrl)), isNotNull);

    // Download and format data from trace for visualization
    final fxtTraceFile = await traceSession
        .terminateAndDownload('$_testNameTitle-tab-opening-test');
    final jsonTraceFile =
        await performance.convertTraceFileToJson(_trace2jsonPath, fxtTraceFile);
    final metricsSpecSet = MetricsSpecSet(
        testName: '$_testNameTitle-tab-opening-test',
        testSuite: _testSuite,
        metricsSpecs: metricsSpecs);

    expect(
        await performance.processTrace(metricsSpecSet, jsonTraceFile,
            converterPath: _catapultConverterPath),
        isNotNull);

    await ermine.driver.requestData('close');
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
  }, skip: skipTests[1]);

  test('Trace performance of rearranging tabs in Simple Browser', () async {
    FlutterDriver browser;
    browser = await ermine.launchSimpleBrowser();

    // Opens yellow.html in the first tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_yellowUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(yellowTabFinder, timeout: _timeout);

    // Opens red.html in the second tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_redUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(redTabFinder, timeout: _timeout);

    // Opens green.html in the third tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_greenUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(greenTabFinder, timeout: _timeout);

    // Opens pink.html in the fourth tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_pinkUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(pinkTabFinder, timeout: _timeout);

    // Opens blue.html in the fifth tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_blueUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(blueTabFinder, timeout: _timeout);

    // Verify 5 tabs launched.
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(yellowTabFinder), isNotNull);
    expect(await browser.getText(redTabFinder), isNotNull);
    expect(await browser.getText(greenTabFinder), isNotNull);
    expect(await browser.getText(pinkTabFinder), isNotNull);
    expect(await browser.getText(blueTabFinder), isNotNull);
    expect(await browser.getText(find.text(_blueUrl)), isNotNull);

    // Verify the current order of tabs before rearranging tabs.
    expect(await _waitForTabArrangement(browser, yellowTabFinder, redTabFinder),
        isTrue,
        reason: 'The Yellow tab is not on the left side of the Red tab:');
    expect(await _waitForTabArrangement(browser, redTabFinder, greenTabFinder),
        isTrue,
        reason: 'The Red tab is not on the left side of the Green tab');
    expect(await _waitForTabArrangement(browser, greenTabFinder, pinkTabFinder),
        isTrue,
        reason: 'The Green tab is not on the left side of the Pink tab');
    expect(await _waitForTabArrangement(browser, greenTabFinder, pinkTabFinder),
        isTrue,
        reason: 'The Green tab is not on the left side of the Pink tab');
    expect(await _waitForTabArrangement(browser, pinkTabFinder, blueTabFinder),
        isTrue,
        reason: 'The Pink tab is not on the left side of the Blue tab');

    // Enable capturing of input_latency metrics.
    measureInputLatency = true;

    // Initialize tracing session for performance tracking.
    final traceSession = await performance.initializeTracing();
    await traceSession.start();

    // Drags the second tab to the right end of the tab list.
    await browser.scroll(yellowTabFinder, 800, 0, Duration(seconds: 1));

    // Stop tracing session
    await traceSession.stop();

    // Verify the order of tabs after rearranging tabs.
    expect(await _waitForTabArrangement(browser, redTabFinder, greenTabFinder),
        isTrue,
        reason: 'The Red tab is not on the left side of the Green tab');
    expect(await _waitForTabArrangement(browser, greenTabFinder, pinkTabFinder),
        isTrue,
        reason: 'The Green tab is not on the left side of the Pink tab');
    expect(await _waitForTabArrangement(browser, greenTabFinder, pinkTabFinder),
        isTrue,
        reason: 'The Green tab is not on the left side of the Pink tab');
    expect(await _waitForTabArrangement(browser, pinkTabFinder, blueTabFinder),
        isTrue,
        reason: 'The Pink tab is not on the left side of the Blue tab');
    expect(
        await _waitForTabArrangement(browser, blueTabFinder, yellowTabFinder),
        isTrue,
        reason: 'The Blue tab is not on the left side of the Yellow tab:');

    // Download and format data from trace for visualization.
    final fxtTraceFile = await traceSession
        .terminateAndDownload('$_testNameTitle-tab-rearranging-test');
    final jsonTraceFile =
        await performance.convertTraceFileToJson(_trace2jsonPath, fxtTraceFile);
    final metricsSpecSet = MetricsSpecSet(
        testName: '$_testNameTitle-tab-rearranging-test',
        testSuite: _testSuite,
        metricsSpecs: metricsSpecs);

    expect(
        await performance.processTrace(metricsSpecSet, jsonTraceFile,
            converterPath: _catapultConverterPath),
        isNotNull);

    await ermine.driver.requestData('close');
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
  }, skip: skipTests[2]);

  test('Trace performance of switching tabs in Simple Browser', () async {
    FlutterDriver browser;
    browser = await ermine.launchSimpleBrowser();

    // Opens yellow.html in the first tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_yellowUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(yellowTabFinder, timeout: _timeout);

    // Opens red.html in the second tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_redUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(redTabFinder, timeout: _timeout);

    // Opens green.html in the third tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_greenUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(greenTabFinder, timeout: _timeout);

    // Opens pink.html in the fourth tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_pinkUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(pinkTabFinder, timeout: _timeout);

    // Opens blue.html in the fifth tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_blueUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(blueTabFinder, timeout: _timeout);

    // Verify 5 tabs launched.
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(yellowTabFinder), isNotNull);
    expect(await browser.getText(redTabFinder), isNotNull);
    expect(await browser.getText(greenTabFinder), isNotNull);
    expect(await browser.getText(pinkTabFinder), isNotNull);
    expect(await browser.getText(blueTabFinder), isNotNull);

    // Verify fifth tab is open.
    expect(await browser.getText(find.text(_blueUrl)), isNotNull);

    // Verify the order of tabs.
    expect(await _waitForTabArrangement(browser, yellowTabFinder, redTabFinder),
        isTrue,
        reason: 'The Yellow tab is not on the left side of the Red tab:');
    expect(await _waitForTabArrangement(browser, redTabFinder, greenTabFinder),
        isTrue,
        reason: 'The Red tab is not on the left side of the Green tab');
    expect(await _waitForTabArrangement(browser, greenTabFinder, pinkTabFinder),
        isTrue,
        reason: 'The Green tab is not on the left side of the Pink tab');
    expect(await _waitForTabArrangement(browser, greenTabFinder, pinkTabFinder),
        isTrue,
        reason: 'The Green tab is not on the left side of the Pink tab');
    expect(await _waitForTabArrangement(browser, pinkTabFinder, blueTabFinder),
        isTrue,
        reason: 'The Pink tab is not on the left side of the Blue tab');

    // Enable capturing of input_latency metrics.
    measureInputLatency = true;

    // Initialize tracing session for performance tracking.
    final traceSession = await performance.initializeTracing();
    await traceSession.start();

    // Switch from fifth tab to first tab.
    // TODO(fxb/76591): Transition pointer interactions to use Sl4f.Input
    await browser.tap(yellowTabFinder);
    await browser.waitFor(find.text(_yellowUrl));

    // Stop tracing session.
    await traceSession.stop();

    // Verify first tab is open.
    expect(await browser.getText(find.text(_yellowUrl)), isNotNull);

    // Download and format data from trace for visualization.
    final fxtTraceFile = await traceSession
        .terminateAndDownload('$_testNameTitle-tab-rearranging-test');
    final jsonTraceFile =
        await performance.convertTraceFileToJson(_trace2jsonPath, fxtTraceFile);
    final metricsSpecSet = MetricsSpecSet(
        testName: '$_testNameTitle-tab-rearranging-test',
        testSuite: _testSuite,
        metricsSpecs: metricsSpecs);

    expect(
        await performance.processTrace(metricsSpecSet, jsonTraceFile,
            converterPath: _catapultConverterPath),
        isNotNull);

    await ermine.driver.requestData('close');
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
  }, skip: skipTests[3]);

  test('Trace performance of closing tabs', () async {
    FlutterDriver browser;
    browser = await ermine.launchSimpleBrowser();

    // Opens yellow.html in the first tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_yellowUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(yellowTabFinder, timeout: _timeout);

    // Opens red.html in the second tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_redUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(redTabFinder, timeout: _timeout);

    // Opens green.html in the third tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_greenUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(greenTabFinder, timeout: _timeout);

    // Opens pink.html in the fourth tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_pinkUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(pinkTabFinder, timeout: _timeout);

    // Opens blue.html in the fifth tab.
    await browser.tap(find.byValueKey('new_tab'));
    await browser.waitFor(find.text(_newTabHintText), timeout: _timeout);
    await input.text(_blueUrl);
    await input.keyPress(kEnterKey);
    await browser.waitUntilNoTransientCallbacks(timeout: _timeout);
    await browser.waitFor(blueTabFinder, timeout: _timeout);

    // Verify 5 tabs launched.
    expect(await browser.getText(newTabFinder), isNotNull);
    expect(await browser.getText(yellowTabFinder), isNotNull);
    expect(await browser.getText(redTabFinder), isNotNull);
    expect(await browser.getText(greenTabFinder), isNotNull);
    expect(await browser.getText(pinkTabFinder), isNotNull);
    expect(await browser.getText(blueTabFinder), isNotNull);
    expect(await browser.getText(find.text(_blueUrl)), isNotNull);

    // Locate tab close.
    final tabCloseFinder = find.byValueKey('tab_close');

    // Enable capturing of input_latency metrics.
    measureInputLatency = true;

    // Initialize tracing session for performance tracking.
    final traceSession = await performance.initializeTracing();
    await traceSession.start();

    // Close second through fifth tabs.
    // TODO(fxb/76591): Transition pointer interactions to use Sl4f.Input
    await browser.tap(tabCloseFinder);
    await browser.waitForAbsent(blueTabFinder);
    await browser.tap(tabCloseFinder);
    await browser.waitForAbsent(pinkTabFinder);
    await browser.tap(tabCloseFinder);
    await browser.waitForAbsent(greenTabFinder);
    await browser.tap(tabCloseFinder);
    await browser.waitForAbsent(redTabFinder);

    // Stop tracing session.
    await traceSession.stop();

    // Verify only first/original tab open.
    expect(await browser.getText(yellowTabFinder), isNotNull);
    expect(await browser.getText(find.text(_yellowUrl)), isNotNull);

    // Download and format data from trace for visualization.
    final fxtTraceFile = await traceSession
        .terminateAndDownload('$_testNameTitle-tab-rearranging-test');
    final jsonTraceFile =
        await performance.convertTraceFileToJson(_trace2jsonPath, fxtTraceFile);
    final metricsSpecSet = MetricsSpecSet(
        testName: '$_testNameTitle-tab-rearranging-test',
        testSuite: _testSuite,
        metricsSpecs: metricsSpecs);

    expect(
        await performance.processTrace(metricsSpecSet, jsonTraceFile,
            converterPath: _catapultConverterPath),
        isNotNull);

    await ermine.driver.requestData('close');
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);
  }, skip: skipTests[4]);
}
