// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter_driver/flutter_driver.dart';
import 'package:flutter_driver_sl4f/flutter_driver_sl4f.dart';
import 'package:logging/logging.dart';
import 'package:sl4f/sl4f.dart';
import 'package:sl4f/trace_processing.dart';
import 'package:test/test.dart';

import '../helpers.dart';

// TODO(fxbug.dev/53934): Re-evaluate what code is shared between tests after adding
// more Flutter benchmarks. Possibly merge this helper with or make it extend
// PerfTestHelper.
class FlutterTestHelper {
  Sl4f sl4f;
  Logger log;

  FlutterDriverConnector connector;
  FlutterDriver driver;
  Performance perf;
  Tiles tiles;

  /// The tile key added by [launchApp]; stored so the tile can be removed by
  /// [_tearDown].
  int _tileKey;

  /// Create a [FlutterTestHelper] with an active sl4f connection.
  ///
  /// Must be called from within a test; will add a teardown and will [fail] the
  /// test if an error occurs when setting up the helper.
  static Future<FlutterTestHelper> make() async {
    final helper = FlutterTestHelper();
    await helper.setUp();
    return helper;
  }

  Future<void> setUp() async {
    enableLoggingOutput();

    log = Logger('FlutterPerfTest');

    sl4f = Sl4f.fromEnvironment();
    await sl4f.startServer();
    if (sl4f == null) {
      fail('Failed to start sl4f.');
    } else {
      log.fine('Started sl4f.');
    }

    perf = Performance(sl4f);
    tiles = Tiles(sl4f);

    addTearDown(_tearDown);
  }

  Future<void> _tearDown() async {
    await driver?.close();
    await connector?.tearDown();

    if (_tileKey != null) {
      await tiles.remove(_tileKey);
      log.fine('Removed tile with key $_tileKey');
    }

    await sl4f.stopServer();
    sl4f.close();
  }

  /// Launch a given app using tiles_ctl.
  Future<void> addTile(String appName, {List<String> args}) async {
    _tileKey = await tiles.addFromUrl(
      'fuchsia-pkg://fuchsia.com/$appName#meta/$appName.cmx',
      args: args,
    );
    log.info('Launched $appName with tile key $_tileKey.');
  }

  /// Connect [driver] to the given [appName].
  ///
  /// Using flutter_driver requires that the app is built with flutter_driver
  /// support and run in an environment with flutter_driver. There are two ways
  /// to do this:
  /// 1) Require flutter_driver in the app. Note that according to the
  ///    documentation for |enableFlutterDriverExtension|, "applications
  ///    intended for release should never include this method." This call will
  ///    not work in release builds of the Flutter app.
  ///      import 'package:flutter_driver/driver_extension.dart';
  ///      void main() { enableFlutterDriverExtension(); }
  /// 2) Use |flutter_driver_extendable| in the app's BUILD file.
  ///    This adds a wrapper around main to call enableFlutterDriverExtension()
  ///    when flutter_driver is enabled in debug builds, so release builds will
  ///    not include flutter_driver.
  ///      import("//build/config.gni")
  ///      import("//build/testing/flutter_driver.gni")
  ///      import("//topaz/runtime/flutter_runner/flutter_app.gni")
  ///      flutter_app("example") {
  ///        flutter_driver_extendable = flutter_driver_enabled
  ///      }
  ///    Then use --args=is_debug=true --args=flutter_driver_enabled=true in
  ///    fx set to enable flutter_driver.
  Future<void> connectFlutterDriver(String appName) async {
    connector = FlutterDriverConnector(sl4f);
    await connector.initialize();

    final isolate = await connector.isolate(appName);
    if (isolate == null) {
      fail('Flutter driver could not find $appName.');
    }
    log.fine('Flutter driver found $appName.');

    driver = await connector.driverForIsolate(appName);
    if (driver == null) {
      fail('Unable to connect to $appName.');
    }
    log.info('Flutter driver connected to $appName.');
  }

  /// A [MetricsProcessor] that finds the startup time of a Flutter app.
  ///
  /// [extraArgs] must contain the key 'flutterAppName'.
  ///
  /// This measures time between the 'StartComponent' event and the end of the
  /// first frame ('vsync callback' event with thread '$flutterAppName.cmx.ui').
  static List<TestCaseResults> flutterStartupTimeMetricsProcessor(
      Model model, Map<String, dynamic> extraArgs) {
    final appName = extraArgs['flutterAppName'];
    if (!(appName is String)) {
      throw ArgumentError('flutterAppName not specified in extraArgs');
    }

    final startComponentEvent = filterEventsTyped<DurationEvent>(
      getAllEvents(model),
      category: 'flutter',
      name: 'StartComponent',
    ).firstWhere(
        (event) =>
            event.args['url'] ==
            'fuchsia-pkg://fuchsia.com/$appName#meta/$appName.cmx',
        orElse: () => null);

    if (startComponentEvent == null) {
      throw ArgumentError(
          'No flutter "StartComponent" event was found in the trace with the '
          'URL "fuchsia-pkg://fuchsia.com/$appName#meta/$appName.cmx".');
    }

    final pid = startComponentEvent.pid;

    // This is expensive if there are events from other flutter apps before
    // the one being measured, but this doesn't happen in the current use case.
    final vsyncCallbackEvent = filterEventsTyped<DurationEvent>(
      getAllEvents(model),
      category: 'flutter',
      name: 'vsync callback',
    ).firstWhere((event) {
      if (event.pid != pid) {
        return false;
      }
      final threadName = model.processes
          .firstWhere((Process process) => process.pid == event.pid)
          .threads
          .firstWhere((Thread thread) => thread.tid == event.tid)
          .name;

      // TODO: Sometimes this thread doesn't get a name, so we also have to
      // accept threads without names for now. It is unclear whether this is a
      // tracing issue (related to fxbug.dev/32554?) or a flutter engine issue.
      // The trace for button_flutter seems to consistently have names for the
      // .platform and .raster threads but not .io or .ui (.ui being the problem
      // here). However, the .io and .ui threads are always(?) present in the
      // clockface-flutter and scroll-flutter benchmarks. In clockface-flutter,
      // meanwhile, the .raster thread is occasionally unnamed (fxbug.dev/56644).
      if (threadName.startsWith('pthread_t:')) {
        Logger('FlutterStartupTimeMetricsProcessor').warning(
            'Assuming thread $threadName is supposed to be $appName.cmx.ui');
        return true;
      }

      return threadName == '$appName.cmx.ui';
    }, orElse: () => null);

    if (vsyncCallbackEvent == null) {
      throw ArgumentError(
          'No flutter "vsync callback" events were found in the trace with the '
          'thread name "$appName.cmx.ui".');
    }

    final TimePoint appLaunch = startComponentEvent.start;
    final TimePoint endOfFirstFrame =
        vsyncCallbackEvent.start + vsyncCallbackEvent.duration;
    final TimeDelta elapsedTime = endOfFirstFrame - appLaunch;

    return [
      TestCaseResults(
        'flutter_startup_time',
        Unit.milliseconds,
        [elapsedTime.toMillisecondsF()],
      ),
    ];
  }

  static const Map<String, MetricsProcessor> metricsRegistry = {
    ...defaultMetricsRegistry,
    'flutter_startup_time': flutterStartupTimeMetricsProcessor,
  };
}
