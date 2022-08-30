// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'package:flutter/material.dart';

/// Defines the light and dark themes for the application.
class AppTheme {
  AppTheme._();

  static ThemeData get lightTheme => ThemeData(
        colorScheme: ColorScheme.light(
          // Used by controls.
          primary: FuchsiaColors.green02,
          // Used by [ListTile]'s trailing text.
          secondary: FuchsiaColors.grey01,
          // Used by control backgrounds.
          background: FuchsiaColors.green02.withOpacity(0.3),
        ),
        fontFamily: 'Roboto Mono',
        // Used for the shell background.
        canvasColor: FuchsiaColors.white,
        // Used for side bar borders.
        dividerColor: Colors.black,
        // Used for side bar background.
        bottomAppBarColor: Colors.white,
        focusColor: Colors.black.withOpacity(0.15),
        hoverColor: Colors.black.withOpacity(0.15),
        disabledColor: FuchsiaColors.grey03,
        // Used for error signals and messages.
        errorColor: FuchsiaColors.red02,
        // Used by ElevatedButton in QuickSettings.
        elevatedButtonTheme: ElevatedButtonThemeData(
          style: ElevatedButton.styleFrom(
            backgroundColor: Colors.black,
            foregroundColor: Colors.white,
            disabledForegroundColor: Colors.white,
            disabledBackgroundColor: Colors.white,
            shadowColor: Colors.transparent,
            elevation: 0,
            padding: EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            shape:
                RoundedRectangleBorder(borderRadius: BorderRadius.circular(4)),
          ),
        ),
        // Used by Switch.
        switchTheme: SwitchThemeData(
          thumbColor: MaterialStateProperty.resolveWith<Color?>(
              (Set<MaterialState> states) {
            if (states.contains(MaterialState.disabled)) {
              return null;
            }
            if (states.contains(MaterialState.selected)) {
              return FuchsiaColors.green02;
            }
            return null;
          }),
          trackColor: MaterialStateProperty.resolveWith<Color?>(
              (Set<MaterialState> states) {
            if (states.contains(MaterialState.disabled)) {
              return null;
            }
            if (states.contains(MaterialState.selected)) {
              return FuchsiaColors.green02;
            }
            return null;
          }),
        ),
        // Used by OutlinedButton in QuickSettings.
        outlinedButtonTheme: OutlinedButtonThemeData(
          style: OutlinedButton.styleFrom(
            foregroundColor: Colors.black,
            disabledForegroundColor: FuchsiaColors.grey03,
            disabledBackgroundColor: FuchsiaColors.grey03,
            side: BorderSide(color: Colors.black),
            padding: EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            shape:
                RoundedRectangleBorder(borderRadius: BorderRadius.circular(0)),
          ),
        ),
        // Used by TextButton in [AlertDialog].
        textButtonTheme: TextButtonThemeData(
          style: TextButton.styleFrom(
            foregroundColor: Colors.black,
            disabledForegroundColor: FuchsiaColors.grey01,
            disabledBackgroundColor: FuchsiaColors.grey01,
            padding: EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          ),
        ),
        tooltipTheme: TooltipThemeData(
          padding: EdgeInsets.all(8),
          textStyle:
              _ErmineTypography.theme.bodyText2!.copyWith(color: Colors.white),
          decoration: BoxDecoration(
            color: FuchsiaColors.black,
            borderRadius: BorderRadius.circular(0),
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
          titleTextStyle: _ErmineTypography.theme.headline5,
          contentTextStyle: _ErmineTypography.theme.bodyText1,
        ),
        // Used for AppBar bottom.
        indicatorColor: FuchsiaColors.grey04,
        textTheme: _ErmineTypography.theme.copyWith(
            caption: _ErmineTypography.theme.caption!
                .copyWith(color: FuchsiaColors.grey01)),
      );

  static ThemeData get darkTheme => ThemeData(
        colorScheme: ColorScheme.dark(
          // Used by control backgrounds.
          primary: FuchsiaColors.green04,
          // Used by [ListTile]'s trailing text.
          secondary: FuchsiaColors.grey04,
          // Used by control backgrounds.
          background: FuchsiaColors.green04.withOpacity(0.3),
        ),
        fontFamily: 'Roboto Mono',
        // Used for the shell background.
        canvasColor: FuchsiaColors.black,
        // Used for side bar borders.
        dividerColor: Colors.white,
        // Used for side bar background.
        bottomAppBarColor: Colors.black,
        focusColor: Colors.white.withOpacity(0.15),
        hoverColor: Colors.white.withOpacity(0.15),
        disabledColor: FuchsiaColors.grey01,
        // Used for error signals and messages.
        errorColor: FuchsiaColors.red04,
        // Used by ElevatedButton in QuickSettings.
        elevatedButtonTheme: ElevatedButtonThemeData(
          style: ElevatedButton.styleFrom(
            backgroundColor: Colors.white,
            foregroundColor: Colors.black,
            disabledForegroundColor: Colors.black,
            disabledBackgroundColor: Colors.black,
            shadowColor: Colors.transparent,
            elevation: 0,
            padding: EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            shape:
                RoundedRectangleBorder(borderRadius: BorderRadius.circular(0)),
          ),
        ),
        // Used by Switch.
        switchTheme: SwitchThemeData(
          thumbColor: MaterialStateProperty.resolveWith<Color?>(
              (Set<MaterialState> states) {
            if (states.contains(MaterialState.disabled)) {
              return null;
            }
            if (states.contains(MaterialState.selected)) {
              return FuchsiaColors.green04;
            }
            return null;
          }),
          trackColor: MaterialStateProperty.resolveWith<Color?>(
              (Set<MaterialState> states) {
            if (states.contains(MaterialState.disabled)) {
              return null;
            }
            if (states.contains(MaterialState.selected)) {
              return FuchsiaColors.green04;
            }
            return null;
          }),
        ),
        // Used by OutlinedButton in QuickSettings.
        outlinedButtonTheme: OutlinedButtonThemeData(
          style: OutlinedButton.styleFrom(
            foregroundColor: Colors.white,
            disabledForegroundColor: FuchsiaColors.grey03,
            disabledBackgroundColor: FuchsiaColors.grey03,
            side: BorderSide(color: Colors.white),
            padding: EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            shape:
                RoundedRectangleBorder(borderRadius: BorderRadius.circular(0)),
          ),
        ),
        // Used by TextButton in [AlertDialog].
        textButtonTheme: TextButtonThemeData(
          style: TextButton.styleFrom(
            foregroundColor: Colors.white,
            disabledForegroundColor: FuchsiaColors.grey02,
            disabledBackgroundColor: FuchsiaColors.grey02,
            padding: EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          ),
        ),
        tooltipTheme: TooltipThemeData(
          padding: EdgeInsets.all(8),
          textStyle:
              _ErmineTypography.theme.bodyText2!.copyWith(color: Colors.black),
          decoration: BoxDecoration(
            color: FuchsiaColors.white,
            borderRadius: BorderRadius.circular(0),
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
          titleTextStyle: _ErmineTypography.theme.headline5,
          contentTextStyle: _ErmineTypography.theme.bodyText1,
        ),
        // Used for AppBar bottom.
        indicatorColor: FuchsiaColors.grey01,
        textTheme: _ErmineTypography.theme.copyWith(
            caption: _ErmineTypography.theme.caption!
                .copyWith(color: FuchsiaColors.grey05)),
      );
}

class _ErmineTypography {
  const _ErmineTypography._();

  static TextTheme theme = TextTheme(
      headline1: TextStyle(
          fontFamily: 'Roboto Mono',
          fontSize: 64,
          fontWeight: FontWeight.normal,
          letterSpacing: -1.5,
          height: 1.125),
      headline2: TextStyle(
          fontFamily: 'Roboto Mono',
          fontSize: 48,
          fontWeight: FontWeight.normal,
          letterSpacing: 0,
          height: 1.17),
      headline3: TextStyle(
          fontFamily: 'Roboto Mono',
          fontSize: 36,
          fontWeight: FontWeight.normal,
          letterSpacing: 0,
          height: 1.3),
      headline4: TextStyle(
          fontFamily: 'Roboto Mono',
          fontSize: 28,
          fontWeight: FontWeight.normal,
          letterSpacing: 0,
          height: 1.14),
      headline5: TextStyle(
          fontFamily: 'Roboto Mono',
          fontSize: 24,
          fontWeight: FontWeight.normal,
          letterSpacing: 0,
          height: 1.17),
      headline6: TextStyle(
          fontFamily: 'Roboto Mono',
          fontSize: 18,
          fontWeight: FontWeight.bold,
          letterSpacing: 0,
          height: 1.3),
      subtitle1: TextStyle(
          fontFamily: 'Roboto Mono',
          fontSize: 16,
          fontWeight: FontWeight.bold,
          letterSpacing: 0,
          height: 1.5),
      subtitle2: TextStyle(
          fontFamily: 'Roboto Mono',
          fontSize: 14,
          fontWeight: FontWeight.bold,
          letterSpacing: 0,
          height: 1.25),
      bodyText1: TextStyle(
          fontFamily: 'Roboto Mono',
          fontSize: 16,
          fontWeight: FontWeight.normal,
          letterSpacing: 0,
          height: 1.4),
      bodyText2: TextStyle(
          fontFamily: 'Roboto Mono',
          fontSize: 14,
          fontWeight: FontWeight.normal,
          letterSpacing: 0,
          height: 1.4),
      button: TextStyle(
          fontFamily: 'Roboto Mono',
          fontSize: 14,
          fontWeight: FontWeight.bold,
          letterSpacing: 0,
          height: 1.14),
      caption: TextStyle(
          fontFamily: 'Roboto Mono',
          fontSize: 12,
          fontWeight: FontWeight.normal,
          letterSpacing: 0,
          color: FuchsiaColors.grey05),
      overline: TextStyle(
          fontFamily: 'Roboto Mono',
          fontSize: 10,
          fontWeight: FontWeight.normal,
          letterSpacing: 0,
          height: 1.4));
}

class FuchsiaColors {
  const FuchsiaColors._();

  static Color black = Color(0xFF283238);
  static Color grey01 = Color(0xFF566168);
  static Color grey02 = Color(0xFF768087);
  static Color grey03 = Color(0xFF909BA3);
  static Color grey04 = Color(0xFFB1B9BE);
  static Color grey05 = Color(0XFFC3CACE);
  static Color grey06 = Color(0XFFDCE0E3);
  static Color white = Color(0XFFF1F3F4);

  static Color green01 = Color(0XFF155450);
  static Color green02 = Color(0XFF0A7965);
  static Color green03 = Color(0XFF269580);
  static Color green04 = Color(0XFF13B294);
  static Color green05 = Color(0XFF07DDC0);
  static Color green06 = Color(0XFF64FFDA);
  static Color green07 = Color(0XFFB9FFEE);
  static Color green08 = Color(0XFFE0FFF8);

  static Color slate01 = Color(0XFF20335C);
  static Color slate02 = Color(0XFF3D4D71);
  static Color slate03 = Color(0XFF5E77A5);
  static Color slate04 = Color(0XFF7F98C7);
  static Color slate05 = Color(0XFFA7BBDE);
  static Color slate06 = Color(0XFFCDDAF2);

  static Color blue01 = Color(0XFF192B95);
  static Color blue02 = Color(0XFF3F51BF);
  static Color blue03 = Color(0XFF546CE2);
  static Color blue04 = Color(0XFF7BCFFF);
  static Color blue05 = Color(0XFFA3DEFF);
  static Color blue06 = Color(0XFFC3E9FF);

  static Color red01 = Color(0XFFAE1F1B);
  static Color red02 = Color(0xffc7241f);
  static Color red03 = Color(0xffe25344);
  static Color red04 = Color(0XFFF0857A);
  static Color red05 = Color(0XFFF1B4AE);
  static Color red06 = Color(0XFFF6DBD8);

  static Color orange01 = Color(0XFFD04E17);
  static Color orange02 = Color(0XFFE2622B);
  static Color orange03 = Color(0XFFEF743F);
  static Color orange04 = Color(0XFFF7A070);
  static Color orange05 = Color(0XFFFFBE99);
  static Color orange06 = Color(0XFFFFE3CA);
}

class ErmineButtonStyle {
  const ErmineButtonStyle._();

  static ButtonStyle elevatedButton(ThemeData theme) => ButtonStyle(
        overlayColor: MaterialStateProperty.resolveWith<Color?>(
            (Set<MaterialState> states) {
          if (states.contains(MaterialState.hovered)) {
            return theme.bottomAppBarColor.withOpacity(0.24);
          }
          if (states.contains(MaterialState.focused)) {
            return theme.bottomAppBarColor.withOpacity(0.38);
          }
          if (states.contains(MaterialState.pressed)) {
            return theme.colorScheme.primary.withOpacity(0.58);
          }
          return null;
        }),
      );

  static ButtonStyle outlinedButton(ThemeData theme) => ButtonStyle(
        overlayColor: MaterialStateProperty.resolveWith<Color?>(
            (Set<MaterialState> states) {
          if (states.contains(MaterialState.hovered)) {
            return theme.dividerColor.withOpacity(0.12);
          }
          if (states.contains(MaterialState.focused)) {
            return theme.dividerColor.withOpacity(0.24);
          }
          if (states.contains(MaterialState.pressed)) {
            return theme.colorScheme.primary.withOpacity(0.58);
          }
          return null;
        }),
      );

  static ButtonStyle textButton(ThemeData theme) => ButtonStyle(
        overlayColor: MaterialStateProperty.resolveWith<Color?>(
            (Set<MaterialState> states) {
          if (states.contains(MaterialState.hovered)) {
            return theme.dividerColor.withOpacity(0.12);
          }
          if (states.contains(MaterialState.focused)) {
            return theme.dividerColor.withOpacity(0.24);
          }
          if (states.contains(MaterialState.pressed)) {
            return theme.colorScheme.primary.withOpacity(0.58);
          }
          return null;
        }),
      );
}
