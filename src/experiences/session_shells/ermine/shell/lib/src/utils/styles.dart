import 'package:flutter/material.dart';

/// Defines a class to hold styling information for Ermine UX.
class ErmineStyle {
  static final ErmineStyle instance = ErmineStyle._internal();

  factory ErmineStyle() => instance;

  ErmineStyle._internal();

  /// Theme used across Ermine UX.
  static ThemeData kErmineTheme = ThemeData(
    brightness: Brightness.dark,
    fontFamily: 'Roboto Mono',
    textTheme: TextTheme(),
    textSelectionTheme:
        TextSelectionThemeData(selectionColor: Colors.grey[400]),
  );

  /// Background color.
  static Color kBackgroundColor = Colors.black;

  /// Color used by overlays.
  static Color kOverlayBackgroundColor = Color(0xFF0C0C0C);

  /// Border color used by overlays.
  static Color kOverlayBorderColor = Color(0xFF262626);

  /// Border width used by overlays.
  static const double kOverlayBorderWidth = 1;

  /// Height of the topbar.
  static const double kTopBarHeight = 40;

  /// Story title color.
  static Color kStoryTitleColor = Colors.white;

  /// Story title background color.
  static Color kStoryTitleBackgroundColor = kBackgroundColor;

  /// Story title height.
  static const double kStoryTitleHeight = 24;

  /// Story border width.
  static const double kBorderWidth = 0;

  /// Screen animation duration in milliseconds. Applies to story fullscreen
  /// transitions, topbar and overview.
  static Duration kScreenAnimationDuration = Duration(milliseconds: 550);

  /// Screen animation curve.
  static Curve kScreenAnimationCurve = Curves.easeOutExpo;

  /// Duration used for items in Ask suggestion list animation.
  static Duration kAskItemAnimationDuration = Duration(milliseconds: 100);

  /// Curve used for items in Ask suggestion list animation.
  static Curve kAskItemAnimationCurve = Curves.easeOutExpo;

  /// Ask bar width.
  static const double kAskBarWidth = 500;

  /// Overview padding around shellements: Ask and Status.
  static const EdgeInsets kOverviewElementPadding = EdgeInsets.all(36);

  /// Recents panel width.
  static const double kRecentsItemWidth = 194;

  /// Recents panel width.
  static const double kRecentsBarWidth = kRecentsItemWidth;
}
