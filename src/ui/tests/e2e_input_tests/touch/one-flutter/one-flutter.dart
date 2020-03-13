// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui' as ui;
import 'package:flutter/widgets.dart';
import 'package:flutter/material.dart';
import 'package:fidl_fuchsia_test_ui/fidl_async.dart' as fidl_test_ui;
import 'package:fuchsia_services/services.dart';

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

  final _responseListener = fidl_test_ui.ResponseListenerProxy();

  _MyHomePageState() {
    StartupContext.fromStartupInfo()
        .incoming
        .connectToService(_responseListener);

    // We inspect the lower-level data packets, instead of using the higher-level gesture library.
    WidgetsBinding.instance.window.onPointerDataPacket =
        (ui.PointerDataPacket packet) {
      for (ui.PointerData data in packet.data) {
        print('Flutter received a pointer: ${data.toStringFull()}');
        if (data.change == ui.PointerChange.down) {
          setState(() {
            _touchCounter++; // Trigger color change on DOWN event.
          });
          _respond(); // Notify test that input was seen.
        }
      }
    };
  }

  void _respond() async {
    await _responseListener.respond();
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
