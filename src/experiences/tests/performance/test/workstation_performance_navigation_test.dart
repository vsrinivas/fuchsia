// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:ermine_driver/ermine_driver.dart';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';

const _catapultConverterPath = 'runtime_deps/catapult_converter';
const _testNameTitle = 'workstation-navigation';
const _testserverUrl =
    'fuchsia-pkg://fuchsia.com/ermine_testserver#meta/ermine_testserver.cmx';
const _testSuite = 'workstation-performance-navigation-test';
const _timeout = Duration(seconds: _timeoutSeconds);
const _timeoutSeconds = 10;
const _trace2jsonPath = 'runtime_deps/trace2json';

void main() {
  sl4f.Dump dump;
  Directory dumpDir;
  ErmineDriver ermine;
  bool measureInputLatency = false;
  sl4f.Performance performance;
  sl4f.Sl4f sl4fDriver;
  sl4f.WebDriverConnector webDriverConnector;

  final metricsSpecs = [
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

    webDriverConnector =
        sl4f.WebDriverConnector('runtime_deps/chromedriver', sl4fDriver);
    await webDriverConnector.initialize();

    // Starts hosting a local http website.
    // ignore: unawaited_futures
    ermine.component.launch(_testserverUrl);
    expect(await ermine.isRunning(_testserverUrl), isTrue);
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
    FlutterDriver browser = await ermine.launchAndWaitForSimpleBrowser();
    const stopUrl = 'http://127.0.0.1:8080/stop';
    await browser.requestData(stopUrl);
    await browser.waitFor(find.text(stopUrl), timeout: _timeout);
    expect(await ermine.isStopped(_testserverUrl), isTrue);

    await ermine.driver.requestData('close');
    await ermine.driver.waitForAbsent(find.text('simple-browser.cmx'));
    expect(await ermine.isStopped(simpleBrowserUrl), isTrue);

    await webDriverConnector?.tearDown();
    await ermine.tearDown();
    await sl4fDriver?.stopServer();
    sl4fDriver?.close();
  });

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
  });
}
