// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';

import 'src/widgets/settings_container.dart';
import 'src/widgets/settings_pane.dart';

void main() {
  setupLogger(name: 'terminal_settings');
  runApp(
    MaterialApp(
      theme: _themeData,
      home: Scaffold(
        body: SettingsContainer([
          ProfilesSetttingsPane(),
        ]),
      ),
    ),
  );
}

final _themeData = ThemeData(
  backgroundColor: Color(0xE6E6E6FF),
  colorScheme: ColorScheme.light(
    background: Color(0xE6E6E6FF),
    primary: Colors.white,
    secondary: Colors.black,
  ),
  fontFamily: 'RobotoMono',
  dividerTheme: DividerThemeData(
    color: Colors.black,
    space: 4.0,
    endIndent: 16.0,
    indent: 16.0,
  ),
);
