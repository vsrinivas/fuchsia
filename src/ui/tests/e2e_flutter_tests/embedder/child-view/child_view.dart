// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:ui' as ui;
import 'package:flutter/material.dart';

Future<void> main(List<String> args) async {
  // TODO(fxbug.dev/59800): Current flutter embedder sometimes initializes the window
  // size and device_pixel_ratio to zero, which causes flutter framework to
  // crash with a runtime exception. The workaround is to wait for window size
  // to be correctly initialized inside [ui.window.onMetricsChanged].
  while (ui.window.physicalSize.isEmpty) {
    print('Awaiting window size...');
    await Future.delayed(Duration(seconds: 1));
  }

  final app = TestApp();
  runApp(app);
}

class TestApp extends StatelessWidget {
  static const _yellow = Color.fromARGB(255, 255, 255, 0);
  static const _pink = Color.fromARGB(255, 255, 0, 255);

  final _backgroundColor = ValueNotifier(_pink);

  @override
  Widget build(BuildContext context) {
    return Listener(
      onPointerDown: (_) => _backgroundColor.value = _yellow,
      child: AnimatedBuilder(
          animation: _backgroundColor,
          builder: (context, _) {
            return Container(
              color: _backgroundColor.value,
            );
          }),
    );
  }
}
