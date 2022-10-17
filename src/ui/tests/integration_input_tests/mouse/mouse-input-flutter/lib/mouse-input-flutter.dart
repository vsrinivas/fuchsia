// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// This is an instrumented test Flutter application which reports mouse movement.
import 'dart:ui' as ui;

import 'package:fidl_fuchsia_ui_test_input/fidl_async.dart' as test_mouse;
import 'package:flutter/material.dart';
import 'package:fuchsia_services/services.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:zircon/zircon.dart';

void main() {
  setupLogger(name: 'mouse-input-flutter');

  log.info('main() started.');
  return runApp(MyApp());
}

List<test_mouse.MouseButton> getPressedButtons(int buttons) {
  var pressed_buttons = <test_mouse.MouseButton>[];
  if (buttons & 0x1 != 0) {
    pressed_buttons.add(test_mouse.MouseButton.first);
  }
  if (buttons & (0x1 >> 1) != 0) {
    pressed_buttons.add(test_mouse.MouseButton.second);
  }
  if (buttons & (0x1 >> 2) != 0) {
    pressed_buttons.add(test_mouse.MouseButton.third);
  }

  return pressed_buttons;
}

test_mouse.MouseEventPhase getPhase(String event_type) {
  switch (event_type) {
    case 'add':
      return test_mouse.MouseEventPhase.add;
    case 'hover':
      return test_mouse.MouseEventPhase.hover;
    case 'down':
      return test_mouse.MouseEventPhase.down;
    case 'move':
      return test_mouse.MouseEventPhase.move;
    case 'up':
      return test_mouse.MouseEventPhase.up;
    default:
      log.severe('Invalid event type: ${event_type}');
  }
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
  // Each mouse down event will bump up the counter, and we change the color.
  int _clickCounter = 0;

  final List<MaterialColor> _colors = <MaterialColor>[
    Colors.red,
    Colors.orange,
    Colors.yellow,
    Colors.green,
    Colors.blue,
    Colors.purple,
  ];

  final _mouseInputListener = test_mouse.MouseInputListenerProxy();

  _MyHomePageState() {
    Incoming.fromSvcPath()
      ..connectToService(_mouseInputListener)
      ..close();

    // We inspect the lower-level data packets, instead of using the higher-level gesture library.
    WidgetsBinding.instance.window.onPointerDataPacket =
        (ui.PointerDataPacket packet) {
      // Record the time when the pointer event was received.
      int nowNanos = System.clockGetMonotonic();

      for (ui.PointerData data in packet.data) {
        log.info('Flutter received a pointer: ${data.toStringFull()}');

        // Ignore non-mouse Pointer events (e.g. touch, stylus).
        if (data.kind == ui.PointerDeviceKind.mouse) {
          if (data.change == ui.PointerChange.down) {
            setState(() {
              _clickCounter++; // Trigger color change on DOWN event.
            });
          }

          log.info(
              'Flutter sent a pointer at: (${data.physicalX}, ${data.physicalY}) with buttons: ${data.buttons}');
          _mouseInputListener.reportMouseInput(
              test_mouse.MouseInputListenerReportMouseInputRequest(
                  // Notify test that input was seen.
                  localX: data.physicalX,
                  localY: data.physicalY,
                  buttons: getPressedButtons(data.buttons),
                  phase: getPhase(data.change.name),
                  timeReceived: nowNanos,
                  wheelXPhysicalPixel: data.scrollDeltaX,
                  wheelYPhysicalPixel: data.scrollDeltaY,
                  componentName: 'mouse-input-flutter'));
        }
      }
    };
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
        appBar: AppBar(),
        body: Center(
            child: MouseRegion(
                child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: <Widget>[
              Container(
                  width: 200,
                  height: 200,
                  decoration: BoxDecoration(
                      color: _colors[_clickCounter % _colors.length],
                      shape: BoxShape.rectangle))
            ]))));
  }
}
