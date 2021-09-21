// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:ui' as ui;

import 'package:fidl_test_touch/fidl_async.dart' as test_touch;
import 'package:flutter/material.dart';
import 'package:fuchsia_services/services.dart';
import 'package:zircon/zircon.dart';

void main() {
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
  const MyHomePage({Key key}) : super(key: key);

  @override
  _MyHomePageState createState() => _MyHomePageState();
}

class _MyHomePageState extends State<MyHomePage> {
  // Each tap down event will bump up the counter, and we change the color.
  int _touchCounter = 0;

  final List<MaterialColor> _colors = <MaterialColor>[
    Colors.red,
    Colors.orange,
    Colors.yellow,
    Colors.green,
    Colors.blue,
    Colors.purple,
  ];

  final _responseListener = test_touch.ResponseListenerProxy();

  _MyHomePageState() {
    Incoming.fromSvcPath()
      ..connectToService(_responseListener)
      ..close();

    // We inspect the lower-level data packets, instead of using the higher-level gesture library.
    WidgetsBinding.instance.window.onPointerDataPacket =
        (ui.PointerDataPacket packet) {
      // Record the time when the pointer event was received.
      int nowNanos = System.clockGetMonotonic();

      for (ui.PointerData data in packet.data) {
        print('Flutter received a pointer: ${data.toStringFull()}');
        if (data.change == ui.PointerChange.down) {
          setState(() {
            _touchCounter++; // Trigger color change on DOWN event.
          });
          _respond(test_touch.PointerData(
              // Notify test that input was seen.
              localX: data.physicalX,
              localY: data.physicalY,
              timeReceived: nowNanos,
              componentName: 'one-flutter'));
        }
      }
    };
  }

  void _respond(test_touch.PointerData pointerData) async {
    await _responseListener.respond(pointerData);
  }

  @override
  Widget build(BuildContext context) => Scaffold(
      appBar: AppBar(),
      body: Center(
          child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: <Widget>[
            Container(
                width: 200,
                height: 200,
                decoration: BoxDecoration(
                    color: _colors[_touchCounter % _colors.length],
                    shape: BoxShape.rectangle))
          ])));
}
