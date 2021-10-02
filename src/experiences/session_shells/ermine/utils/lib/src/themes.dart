// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

/// Defines the light and dark themes for the application.
class AppTheme {
  AppTheme._();

  static ThemeData get lightTheme => ThemeData(
        colorScheme: ColorScheme.light(
          // Used by controls.
          primary: Color(0xff0a7965),
          // Used by [ListTile]'s trailing text.
          secondary: Colors.grey[800]!,
          // Used by control backgrounds.
          background: Color(0xff0a7965).withOpacity(0.3),
        ),
        fontFamily: 'Roboto Mono',
        // Used for the shell background.
        canvasColor: Colors.grey[200],
        // Used for side bar borders.
        dividerColor: Colors.black,
        // Used for side bar background.
        bottomAppBarColor: Colors.white,
        focusColor: Colors.black.withOpacity(0.15),
        hoverColor: Colors.black.withOpacity(0.15),
        disabledColor: Colors.grey[500],
        // Used for error signals and messages.
        errorColor: Color(0xffc7241f),
        // Used by Switch
        toggleableActiveColor: Color(0xff0a7965),
        // Used by ElevatedButton in QuickSettings.
        elevatedButtonTheme: ElevatedButtonThemeData(
          style: ElevatedButton.styleFrom(
            primary: Colors.black,
            onPrimary: Colors.white,
            onSurface: Colors.white,
            shadowColor: Colors.transparent,
            elevation: 0,
            padding: EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            shape:
                RoundedRectangleBorder(borderRadius: BorderRadius.circular(4)),
          ),
        ),
        // Used by OutlinedButton in QuickSettings.
        outlinedButtonTheme: OutlinedButtonThemeData(
          style: OutlinedButton.styleFrom(
            primary: Colors.black,
            side: BorderSide(color: Colors.black),
            padding: EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            shape:
                RoundedRectangleBorder(borderRadius: BorderRadius.circular(4)),
          ),
        ),
        // Used by TextButton in [AlertDialog].
        textButtonTheme: TextButtonThemeData(
          style: TextButton.styleFrom(
            primary: Colors.black,
            onSurface: Colors.grey[700],
            padding: EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          ),
        ),
        tooltipTheme: TooltipThemeData(
          padding: EdgeInsets.all(8),
          decoration: BoxDecoration(
            color: Colors.grey[900],
            borderRadius: BorderRadius.circular(4),
          ),
        ),
        // Used by AppBar under QuickSettings details screen.
        appBarTheme: AppBarTheme(
          backgroundColor: Colors.white,
          iconTheme: IconThemeData(color: Colors.black),
          titleTextStyle: TextStyle(color: Colors.black, fontSize: 24),
          elevation: 0,
          shadowColor: Colors.transparent,
        ),
        dialogTheme: DialogTheme(
          backgroundColor: Colors.white,
          elevation: 0,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(4),
            side: BorderSide(color: Colors.black),
          ),
        ),
        // Used for AppBar bottom.
        indicatorColor: Colors.grey[400],
        textTheme: TextTheme(
          // Used by [ListTile]'s subtitle.
          caption: TextStyle(color: Colors.grey[800]),
        ),
      );

  static ThemeData get darkTheme => ThemeData(
        colorScheme: ColorScheme.dark(
          // Used by control backgrounds.
          primary: Color(0xff13b294),
          // Used by [ListTile]'s trailing text.
          secondary: Colors.grey[400]!,
          // Used by control backgrounds.
          background: Color(0xff13b294).withOpacity(0.3),
        ),
        fontFamily: 'Roboto Mono',
        // Used for the shell background.
        canvasColor: Colors.grey[900]!,
        // Used for side bar borders.
        dividerColor: Colors.white,
        // Used for side bar background.
        bottomAppBarColor: Colors.black,
        focusColor: Colors.white.withOpacity(0.15),
        hoverColor: Colors.white.withOpacity(0.15),
        disabledColor: Colors.grey[700],
        // Used for error signals and messages.
        errorColor: Color(0xffe25344),
        // Used by Switch.
        toggleableActiveColor: Color(0xff13b294),
        // Used by ElevatedButton in QuickSettings.
        elevatedButtonTheme: ElevatedButtonThemeData(
          style: ElevatedButton.styleFrom(
            primary: Colors.white,
            onPrimary: Colors.black,
            onSurface: Colors.black,
            shadowColor: Colors.transparent,
            elevation: 0,
            padding: EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            shape:
                RoundedRectangleBorder(borderRadius: BorderRadius.circular(4)),
          ),
        ),
        // Used by OutlinedButton in QuickSettings.
        outlinedButtonTheme: OutlinedButtonThemeData(
          style: OutlinedButton.styleFrom(
            primary: Colors.white,
            side: BorderSide(color: Colors.white),
            padding: EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            shape:
                RoundedRectangleBorder(borderRadius: BorderRadius.circular(4)),
          ),
        ),
        // Used by TextButton in [AlertDialog].
        textButtonTheme: TextButtonThemeData(
          style: TextButton.styleFrom(
            primary: Colors.white,
            onSurface: Colors.grey[600],
            padding: EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          ),
        ),
        tooltipTheme: TooltipThemeData(
          padding: EdgeInsets.all(8),
          decoration: BoxDecoration(
            color: Colors.grey[100],
            borderRadius: BorderRadius.circular(4),
          ),
        ),
        // Used by AppBar under QuickSettings details screen.
        appBarTheme: AppBarTheme(
          backgroundColor: Colors.black,
          iconTheme: IconThemeData(color: Colors.white),
          titleTextStyle: TextStyle(color: Colors.white, fontSize: 24),
          elevation: 0,
          shadowColor: Colors.transparent,
        ),
        dialogTheme: DialogTheme(
          backgroundColor: Colors.black,
          elevation: 0,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(4),
            side: BorderSide(color: Colors.white),
          ),
        ),
        // Used for AppBar bottom.
        indicatorColor: Colors.grey[700],
        textTheme: TextTheme(
          // Used by [ListTile]'s subtitle.
          caption: TextStyle(color: Colors.grey[400]),
        ),
      );
}
