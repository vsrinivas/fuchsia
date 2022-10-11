// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math';

// ignore_for_file: import_of_legacy_library_into_null_safe
import 'package:fidl_fuchsia_input/fidl_async.dart' as input;
import 'package:fidl_fuchsia_ui_input3/fidl_async.dart' as input3;
import 'package:fidl_fuchsia_input/fidl_async.dart';
import 'package:fidl_fuchsia_ui_input3/fidl_async.dart' hide KeyEvent;
import 'package:flutter_driver/flutter_driver.dart';
import 'package:flutter_driver_sl4f/flutter_driver_sl4f.dart';
import 'package:image/image.dart' hide Point;
import 'package:sl4f/sl4f.dart';
import 'package:test/test.dart';

const simpleBrowserUrl =
    'fuchsia-pkg://fuchsia.com/simple-browser#meta/simple-browser.cmx';
const terminalUrl = 'fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cm';
const kLoginInspectSelector =
    'core/session-manager/session\\:session/workstation_session/login_shell';
const kErmineInspectSelector =
    'core/session-manager/session\\:session/workstation_session/login_shell/ermine_shell';

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

  FlutterDriver? _driver;
  FlutterDriver? _login;
  final FlutterDriverConnector _connector;

  /// Constructor.
  ErmineDriver(this.sl4f)
      : _connector = FlutterDriverConnector(sl4f),
        _component = Component(sl4f);

  /// The instance of [FlutterDriver] that is connected to Ermine flutter app.
  FlutterDriver get driver => _driver!;

  /// The instance of [FlutterDriver] that is connected to Login flutter app.
  FlutterDriver get login => _login!;

  /// The instance of [Component] that is connected to the DUT.
  Component get component => _component;

  /// Set up the test environment for Ermine.
  ///
  /// This restarts the workstation session and connects to the running instance
  /// of Ermine using FlutterDriver.
  Future<void> setUp() async {
    // Restart the workstation session.
    // TODO(fxb/87746): Temporarily remove to see if it causes the issue.
    // final result = await sl4f.ssh.run('session_control restart');
    // if (result.exitCode != 0) {
    //   fail('failed to restart workstation session.');
    // }

    // Initialize Ermine's flutter driver and web driver connectors.
    await _connector.initialize();
    print('Flutter driver connector initialized');

    // Connect to login shell's flutter driver.
    _login = await connectToFlutterDriver('login', kLoginInspectSelector,
        (snapshot) => snapshot['ready'] == true);
    if (_login == null) {
      fail('Unable to connect to login shell.');
    }
    print('Connected to login shell');

    // Now connect to ermine.
    _driver = await authenticate();
    print('Driver is connected to Ermine');
  }

  /// Closes [FlutterDriverConnector] and performs cleanup.
  Future<void> tearDown() async {
    final result = await sl4f.ssh.run('session_control restart');
    if (result.exitCode != 0) {
      fail('failed to restart workstation session: ${result.exitCode},'
          ' stderr: ${result.stderr}, stdout: ${result.stdout}');
    }
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

  /// Launch a component given its name from the App Launcher.
  Future<void> launchFromAppLauncher(String title) async {
    // Bring up the App Launcher overlay.
    await driver.requestData('launcher');
    await driver.waitUntilNoTransientCallbacks();
    await waitForOverlays();

    final titleFinder = find.descendant(
      of: find.byType('AppLauncher'),
      matching: find.text(title),
    );

    await driver.tap(titleFinder);
    await driver.waitUntilNoTransientCallbacks();
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

  /// Waits until the overlays are visible, timesout otherwise.
  Future<bool> waitForOverlays() {
    return waitFor(() async {
      final data = await snapshot;
      return data.overlaysVisible;
    });
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
    await input.tap(point);
  }

  /// Invoke a two key keyboard shortcut.
  Future<void> twoKeyShortcut(Key modifier, Key key) async {
    const key1Press = Duration(milliseconds: 100);
    const key2Press = Duration(milliseconds: 200);
    const key2Release = Duration(milliseconds: 400);
    const key1Release = Duration(milliseconds: 600);

    final input = Input(sl4f);
    await input.sendKeyEvents([
      InputKeyEvent(modifier, key1Press, KeyEventType.pressed),
      InputKeyEvent(key, key2Press, KeyEventType.pressed),
      InputKeyEvent(key, key2Release, KeyEventType.released),
      InputKeyEvent(modifier, key1Release, KeyEventType.released),
    ].map((e) => e.toJson()).toList());
    await driver.waitUntilNoTransientCallbacks();
  }

  /// Invoke a three key keyboard shortcut.
  Future<void> threeKeyShortcut(Key modifier1, Key modifier2, Key key) async {
    const key1Press = Duration(milliseconds: 100);
    const key2Press = Duration(milliseconds: 200);
    const key3Press = Duration(milliseconds: 300);
    const key3Release = Duration(milliseconds: 500);
    const key2Release = Duration(milliseconds: 600);
    const key1Release = Duration(milliseconds: 700);

    final input = Input(sl4f);
    await input.sendKeyEvents([
      InputKeyEvent(modifier1, key1Press, KeyEventType.pressed),
      InputKeyEvent(modifier2, key2Press, KeyEventType.pressed),
      InputKeyEvent(key, key3Press, KeyEventType.pressed),
      InputKeyEvent(key, key3Release, KeyEventType.released),
      InputKeyEvent(modifier2, key2Release, KeyEventType.released),
      InputKeyEvent(modifier1, key1Release, KeyEventType.released),
    ].map((e) => e.toJson()).toList());
    await driver.waitUntilNoTransientCallbacks();
  }

  Future<Rectangle> getViewRect(String viewUrl,
      [Duration timeout = waitForTimeout]) async {
    final view = await waitForView(viewUrl, timeout: timeout);
    return view.viewport;
  }

  /// Finds the first launched component given its [viewUrl] and returns it's
  /// Inspect data. Waits for [timeout] duration for view to launch. If
  /// [testForFocus] is true, waits for focused signal.
  Future<ViewSnapshot> waitForView(String viewUrl,
      {bool testForFocus = false, Duration timeout = waitForTimeout}) async {
    return waitFor(() async {
      final views = await launchedViews(filterByUrl: viewUrl);
      if (views.isEmpty) {
        return null;
      }
      if (testForFocus) {
        return views.first.focused ? views.first : null;
      }
      return views.first;
    }, timeout: timeout);
  }

  Future<bool> waitForViewAbsent(String viewUrl,
      [Duration timeout = waitForTimeout]) async {
    return waitFor(() async {
      final views = await launchedViews(filterByUrl: viewUrl);
      return views.isEmpty;
    });
  }

  Future<Map<String, dynamic>> inspectSnapshot(
    String componentSelector, {
    Duration timeout = waitForTimeout,
    bool predicate(Map<String, dynamic> snapshot)?,
  }) {
    return waitFor(() async {
      final snapshot = await Inspect(sl4f).snapshotRoot(componentSelector);
      // Supply a default predicate that simply returns true.
      predicate ??= (_) => true;
      // ignore: unnecessary_null_comparison
      return snapshot == null || snapshot.isEmpty || !predicate!(snapshot)
          ? null
          : snapshot;
    }, timeout: timeout);
  }

  /// Returns the current shell snapshot from inspect data.
  Future<ShellSnapshot> get snapshot async {
    final data = await driver.requestData('inspect');
    return ShellSnapshot(json.decode(data));
  }

  /// Returns the last keyboard shortcut action received by ermine shell.
  Future<String> get lastAction async => (await snapshot).lastAction;

  /// Waits for last action to match the supplied value.
  Future<bool> waitForAction(String action,
      {Duration timeout = waitForTimeout}) async {
    return waitFor(() async {
      return (await lastAction) == action;
    }, timeout: timeout);
  }

  /// Returns the list of launched views from inspect data.
  Future<List<ViewSnapshot>> get views async => (await snapshot).views;

  /// Returns [Inspect] data for all launched views.
  Future<List<ViewSnapshot>> launchedViews({String? filterByUrl}) async {
    final allViews = await views;
    return filterByUrl == null
        ? allViews
        : allViews.where((view) => view.url == filterByUrl).toList();
  }

  /// Take a screenshot of a View given its screen co-ordinates.
  Future<Image> screenshot(Rectangle rect) async {
    final scenic = Scenic(sl4f);
    final image = await scenic.takeScreenshot(dumpName: 'ermine');
    return copyCrop(
      image,
      rect.left.toInt(),
      rect.top.toInt(),
      rect.width.toInt(),
      rect.height.toInt(),
    );
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
    if (goldenImage == null) {
      return 1;
    }
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
  /// The computation is repeated until it returns a boolean true or non-null
  /// result or the timeout expires. It throws a [TimeoutException] if the
  /// timeout expires.
  Future<T> waitFor<T>(WaitForCompletion<T?> completion,
      {Duration timeout = waitForTimeout}) async {
    final end = DateTime.now().add(timeout);
    T? result;
    while (DateTime.now().isBefore(end)) {
      result = await completion();
      if (result == null || result is bool && result == false) {
        // Add a delay so as not to spam the system.
        await Future.delayed(Duration(seconds: 1));
        continue;
      }
      return result;
    }
    // We ran out of time.
    throw TimeoutException('waitFor timeout expired', timeout);
  }

  /// Connects to [FlutterDriver] for isolate [name] with [selector];
  Future<FlutterDriver> connectToFlutterDriver(String name, String selector,
      [bool predicate(Map<String, dynamic> snapshot)?]) async {
    // Connect to Login flutter app.
    print('Connecting to $name flutter isolate');
    await inspectSnapshot(selector, predicate: predicate);
    final driver = await _connector.driverForIsolateBySelector(name, selector);

    // Wait for shell to draw first frame.
    await driver.waitUntilFirstFrameRasterized();
    print('The first frame has been rasterized');

    // Wait until rendering stabilizes and animations settle.
    await driver.waitUntilNoTransientCallbacks();
    print('No further transient callbacks. $name is ready.');
    return driver;
  }

  /// If the workstation build is enabled for authentication through the login
  /// shell, go through the create password or login flow.
  ///
  /// Returns [true] is successfully performed authentication flows.
  Future<FlutterDriver> authenticate() async {
    // Check if login shell's inspect data is published and is 'ready'.
    print('Starting authentication flow.');
    final snapshot = await inspectSnapshot(kLoginInspectSelector,
        predicate: (snapshot) => snapshot['ready'] == true);

    // Check if OOBE is skipped or shown.
    if (snapshot['launchOOBE'] == true) {
      // Check if auth status is authenticated. If no, start auth flow.
      if (snapshot['authenticated'] != true) {
        // Check if create password is visible.
        print('Performing authentication...');

        const testPassword = '11223344';
        if (snapshot['screen'] == 'password') {
          print('Creating password...');

          // Tap on the first password text field.
          var passwordTextField = find.byValueKey('password1');
          await enterText(testPassword,
              driver: login, textField: passwordTextField);
          print('password 1 done');

          // Tap on the second password text field.
          passwordTextField = find.byValueKey('password2');
          await enterText(testPassword,
              driver: login, textField: passwordTextField);
          print('password 2 done');

          // Tap the Set Password button and wait for auth to succeed.
          await login.tap(find.byValueKey('setPassword'));
          await login.waitForAbsent(find.byType('Password'),
              timeout: Duration(minutes: 2));
          print('Password set');

          // Tap through the 'Start Workstation' screen.
          await login.tap(find.byValueKey('startWorkstation'));
          await login.waitForAbsent(find.byType('Ready'));
        } else {
          print('Adding login credentials...');
          // Tap on the password text field.
          var passwordTextField = find.byValueKey('password');
          await enterText(testPassword,
              driver: login, textField: passwordTextField);
          print('password entered');

          // Tap the Login button and wait for auth to succeed.
          await login.tap(find.byValueKey('login'));
          await login.waitForAbsent(find.byType('Login'),
              timeout: Duration(minutes: 2));
          print('Login done');
        }
      }
    }

    print('Starting ermine shell');
    await Future.delayed(Duration(seconds: 2));
    await inspectSnapshot(kLoginInspectSelector,
        predicate: (snapshot) => snapshot['ermineReady'] == true);
    // We should land on the Ermine shell.
    await login.waitUntilNoTransientCallbacks();
    await login.waitFor(find.byType('ErmineApp'));

    return connectToFlutterDriver('ermine', kErmineInspectSelector);
  }

  /// Performs a logout followed by login authentication flow.
  ///
  /// This is done in one flow to ensure ermine's flutter [_driver] is valid at
  /// the end of the flow.
  Future<void> logoutAndLogin() async {
    // Ensure that ermine shell is running.
    print('Starting logout->login flow');
    await login.waitFor(find.byType('ErmineApp'));
    await inspectSnapshot(kErmineInspectSelector);
    await driver.waitUntilNoTransientCallbacks();

    // Send logout action
    print('Sending logout request');
    await driver.requestData('logout');
    await driver.waitUntilNoTransientCallbacks();

    // Tap 'Log out' button to confirm logout dialog, but don't wait for it to
    // complete because ermine is killed.
    print('Tapping logout button in logout confirmation dialog');
    // ignore: unawaited_futures
    driver.tap(find.text('LOGOUT')).catchError((_) {});
    // ignore: unawaited_futures
    driver.close();
    await Future.delayed(Duration(seconds: 2));

    // If launchOOBE is true, wait for the login screen.
    print('Waiting for login screen');
    final snapshot = await inspectSnapshot(kLoginInspectSelector,
        predicate: (snapshot) => snapshot['ready'] == true);
    if (snapshot['launchOOBE'] == true) {
      await login.waitUntilNoTransientCallbacks();
      await login.waitFor(find.byType('Login'));
    }

    // Now log back in and set the new flutter driver connection to ermine.
    _driver = await authenticate();
  }

  /// Enters text using [Input] to [textField] using [driver].
  Future<void> enterText(
    String text, {
    required FlutterDriver driver,
    required SerializableFinder textField,
  }) async {
    final input = Input(sl4f);
    // Tap on the text field.
    await driver.tap(textField);
    await input.text(text);
    // Wait for input to make to the text field.
    await waitFor(() async {
      return (await driver.getText(textField)) == text;
    });
  }
}

/// Holds Ermine shell state which is derived from inspect data.
class ShellSnapshot {
  final Map<String, dynamic> inspectData;

  ShellSnapshot(this.inspectData);

  int get numViews => inspectData['numViews'] ?? 0;
  bool get appBarVisible => inspectData['appBarVisible'] == true;
  bool get sideBarVisible => inspectData['sideBarVisible'] == true;
  bool get overlaysVisible => inspectData['overlaysVisible'] == true;
  String get lastAction => inspectData['lastAction'] ?? '';
  bool get darkMode => inspectData['darkMode'] ?? true;
  ViewSnapshot? get activeView =>
      numViews > 0 ? views[inspectData['activeView'] ?? 0] : null;

  List<ViewSnapshot> get views {
    final result = <ViewSnapshot>[];
    for (int i = 0; i < numViews; i++) {
      result.add(ViewSnapshot(inspectData['view-$i']));
    }
    return result;
  }
}

/// Holds a launched view's state which is derived from inspect data.
class ViewSnapshot {
  final Map<String, dynamic> inspectData;

  ViewSnapshot(this.inspectData);

  bool get focused => inspectData['focused'] == true;
  String get title => inspectData['title'] ?? '';
  String get url => inspectData['url'] ?? '';

  Rectangle get viewport {
    final viewRect =
        inspectData['viewportLTRB']?.split(',')?.map(num.parse).toList();
    if (viewRect != null) {
      return Rectangle.fromPoints(
        Point(viewRect[0], viewRect[1]),
        Point(viewRect[2], viewRect[3]),
      );
    }
    return Rectangle(0, 0, 0, 0);
  }
}

/// Describes  single key event to pass into input_facade.
class InputKeyEvent {
  final input.Key _key;
  final Duration _durationSinceStart;
  final input3.KeyEventType _type;

  /// Creates a new [KeyEvent].
  InputKeyEvent(this._key, this._durationSinceStart, this._type);

  /// Return this as a primitive object that can be JSON-encoded.
  Map<String, dynamic> toJson() => {
        'key': _key.$value,
        'duration_millis': _durationSinceStart.inMilliseconds,
        'type': _type.$value,
      };
}
