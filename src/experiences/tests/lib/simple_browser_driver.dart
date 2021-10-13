// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

// ignore_for_file: import_of_legacy_library_into_null_safe

import 'package:ermine_driver/ermine_driver.dart';
import 'package:flutter_driver/flutter_driver.dart';
import 'package:flutter_driver_sl4f/flutter_driver_sl4f.dart';
import 'package:test/test.dart';

class SimpleBrowserDriver {
  final ErmineDriver _ermine;
  final FlutterDriverConnector _connector;
  FlutterDriver? _browser;

  FlutterDriver get driver => _browser!;

  SimpleBrowserDriver(this._ermine)
      : _connector = FlutterDriverConnector(_ermine.sl4f);

  /// Launches a simple browser and returns a [FlutterDriver] connected to it.
  Future<void> launchSimpleBrowser() async {
    expect(await _ermine.launch(simpleBrowserUrl), isTrue);
    print('Launched a browser');

    // Initializes the browser's flutter driver connector.
    await _connector.initialize();
    print('Initialized a flutter driver connector for the browser.');

    // Checks if Simple Browser is running.
    // TODO(fxb/66577): Get the last isolate once it's supported by
    // [FlutterDriverConnector] in flutter_driver_sl4f.dart
    final browserIsolate = await _connector.isolate('simple-browser');
    // ignore: unnecessary_null_comparison
    if (browserIsolate == null) {
      fail('couldn\'t find simple browser.');
    }
    print('Checked that the browser is running.');

    // Connects to the browser.
    // TODO(fxb/66577): Get the driver of the last isolate once it's supported by
    // [FlutterDriverConnector] in flutter_driver_sl4f.dart
    _browser = await _connector.driverForIsolate('simple-browser');
    // ignore: unnecessary_null_comparison
    if (_browser == null) {
      fail('unable to connect to simple browser.');
    }
    print('Connected the browser to a flutter driver.');
  }

  /// Launches a simple browser and sets up options for test convenience.
  ///
  /// Opens another new tab as soon as the browser is launched, unless you set
  /// [openNewTab] to false. Contrarily, set [fullscreen] to true if you want
  /// the browser to expand its size to full-screen upon its launch.
  /// Also, you can set the text entry emulation of the browser's flutter driver
  /// using [enableTextEntryEmulation], which has false by default.
  Future<void> launchAndWaitForSimpleBrowser({
    bool openNewTab = true,
    bool enableTextEntryEmulation = false,
  }) async {
    await launchSimpleBrowser();

    if (_browser != null) {
      // Set the flutter driver's text entry emulation.
      await _browser!.setTextEntryEmulation(enabled: enableTextEntryEmulation);
      print('Text entry emulation is enabled for the browser.');

      // Opens another tab other than the tab opened on browser's launch,
      // if required.
      if (openNewTab) {
        final addTab = find.byValueKey('new_tab');
        await _browser!.waitFor(addTab);

        await _browser!.tap(addTab);
        await _browser!
            .waitFor(find.text('NEW TAB'), timeout: Duration(seconds: 10));
        print('Opened a new tab');
      } else {
        await _browser!
            .waitFor(find.text('     SEARCH'), timeout: Duration(seconds: 10));
        print('The first tab is ready.');
      }

      await _browser!.waitUntilFirstFrameRasterized();
      await _browser!.waitUntilNoTransientCallbacks();
      print('No further transient callbacks.');
    }
  }

  Future<void> tearDown() async {
    await _browser?.close();
    await _connector.tearDown();
  }
}
