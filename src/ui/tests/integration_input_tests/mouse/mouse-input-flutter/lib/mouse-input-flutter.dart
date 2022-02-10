// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// This is an instrumented test Flutter application which reports mouse movement.
import 'package:fidl_test_mouse/fidl_async.dart' as test_mouse;
import 'package:flutter/material.dart';
import 'package:fuchsia_services/services.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:zircon/zircon.dart';

void main() {
  setupLogger(name: 'mouse-input-flutter');

  log.info('main() started.');
  return runApp(MyApp());
}

class MyApp extends StatelessWidget {
  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'Flutter App',
        theme: ThemeData(
          primarySwatch: Colors.blue,
        ),
        home: MyHomePage(),
      );
}

class MyHomePage extends StatefulWidget {
  const MyHomePage() : super();

  @override
  _MyHomePageState createState() => _MyHomePageState();
}

class _MyHomePageState extends State<MyHomePage> {
  final _responseListener = test_mouse.ResponseListenerProxy();

  _MyHomePageState() {
    Incoming.fromSvcPath()
      ..connectToService(_responseListener)
      ..close();
  }

  // Note: There might not be a strong ordering between `initState`
  // and view connection, even for Flatland. This content could be
  // perceived as having been prepared for display presentation,
  // prior to that content getting placed *on* display.
  //
  // This function should be removed once there are injected
  // mouse inputs producing real `PointerData` to send via  `_respond`.
  @override
  void initState() {
    super.initState();

    // Record the time when the pointer event was received.
    int nowNanos = System.clockGetMonotonic();
    _respond(test_mouse.PointerData(
        // Notify test that input is available with clean state.
        localX: 0,
        localY: 0,
        timeReceived: nowNanos,
        componentName: 'mouse-input-flutter'));
  }

  void _respond(test_mouse.PointerData pointerData) async {
    log.info(
        'Flutter sent a pointer: (${pointerData.localX}, ${pointerData.localY})');
    await _responseListener.respond(pointerData);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
        appBar: AppBar(),
        body: Center(
            child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: <Widget>[
              Container(
                  width: 200,
                  height: 200,
                  decoration: BoxDecoration(shape: BoxShape.rectangle))
            ])));
  }
}
