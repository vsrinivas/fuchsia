// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

/// Defines the light and dark themes for the application.
class AppTheme {
  AppTheme._();

  static ThemeData get lightTheme => ThemeData(
        colorScheme: ColorScheme.light(
          // Used by control backgrounds.
          primary: Colors.pinkAccent,
          // Used by controls.
          secondary: Colors.grey[800]!,
        ),
        fontFamily: 'Roboto Mono',
        // Used for the shell background.
        canvasColor: Colors.grey[400],
        // Used for side bar borders.
        dividerColor: Colors.black,
        // Used for side bar background.
        bottomAppBarColor: Colors.grey[100],
        // Used for error signals and messages.
        errorColor: Colors.red[600],
        // Used by Switch
        toggleableActiveColor: Colors.pink[400],
        // Used by OutlinedButton in QuickSettings.
        outlinedButtonTheme: OutlinedButtonThemeData(
          style: OutlinedButton.styleFrom(
            primary: Colors.black,
            side: BorderSide(color: Colors.black),
          ),
        ),
        tooltipTheme: TooltipThemeData(
          padding: EdgeInsets.all(16),
        ),
        // Used by AppBar under QuickSettings details screen.
        appBarTheme: AppBarTheme(
          backgroundColor: Colors.grey[100],
          iconTheme: IconThemeData(color: Colors.black),
          titleTextStyle: TextStyle(color: Colors.black),
        ),
        // Used for AppBar bottom.
        indicatorColor: Colors.grey[400],
      );

  static ThemeData get darkTheme => ThemeData(
        colorScheme: ColorScheme.dark(
          // Used by control backgrounds.
          primary: Colors.pinkAccent,
          // Used by controls.
          secondary: Colors.grey[200]!,
        ),
        fontFamily: 'Roboto Mono',
        // Used for the shell background.
        canvasColor: Color.fromRGBO(0x12, 0x12, 0x12, 1),
        // Used for side bar borders.
        dividerColor: Colors.white,
        // Used for side bar background.
        bottomAppBarColor: Colors.black,
        // Used for error signals and messages.
        errorColor: Colors.red[500],
        // Used by Switch.
        toggleableActiveColor: Colors.pink[400],
        // Used by OutlinedButton in QuickSettings.
        outlinedButtonTheme: OutlinedButtonThemeData(
          style: OutlinedButton.styleFrom(
            primary: Colors.white,
            side: BorderSide(color: Colors.white),
          ),
        ),
        tooltipTheme: TooltipThemeData(
          padding: EdgeInsets.all(16),
        ),
        // Used by AppBar under QuickSettings details screen.
        appBarTheme: AppBarTheme(
          backgroundColor: Colors.black,
          iconTheme: IconThemeData(color: Colors.white),
          titleTextStyle: TextStyle(color: Colors.white),
        ),
        // Used for AppBar bottom.
        indicatorColor: Color.fromRGBO(0x28, 0x28, 0x28, 1),
      );
}
